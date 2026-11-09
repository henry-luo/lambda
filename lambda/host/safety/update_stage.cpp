#include "update_stage.h"
#include "update_records.h"
#include "storage_tree.h"
#include "state_wire.h"
#include "lib/digest.h"
#include "lib/hex.h"
#include "lib/hashmap.h"
#include "lib/mem.h"
#include "lib/strbuf.h"
#include "lib/time_util.h"
#include <errno.h>
#include <string.h>
#ifndef _WIN32
#include <sys/file.h>
#include <unistd.h>
#endif

enum { STAGE_ENTRIES = 4096, STAGE_INTENT_BYTES = 272, STAGE_MANIFEST_BYTES = 8 * 1024 * 1024,
    STAGE_DESCRIPTION_BYTES = 262144, STAGE_TREE_ENTRIES = STAGE_ENTRIES + 6 };
struct StageIntent {
    uint8_t id[16], binding[32], authority[32], artifact[32], description[32], manifest[32];
    uint64_t device, inode, image_device, image_inode, bytes;
    uint32_t ready, entries;
};
struct SecurityStagedUpdate {
    FileAnchor* root;
    FileAnchor* directory;
    FileAnchor* image;
    FILE* claim;
    StageIntent intent;
    FileRevision revision;
};
struct StageEntry { char* path; char* key; bool directory, executable; uint64_t size; uint8_t digest[32]; };
struct StageCall {
    SecurityUpdateJob* job;
    SecurityVerifiedUpdateView input;
    SecurityUpdateStageReport* report;
    SecurityStagedUpdate* stage;
    ArrayList* entries;
    HashMap* names;
    StrBuf* manifest;
    uint8_t binding[32], authority[32], artifact[32], description[32];
    uint64_t bytes;
    uint32_t seconds;
};
static SecurityUpdateResult stage_error() {
    if (errno == ENOENT) return SECURITY_UPDATE_MISSING;
    if (errno == EWOULDBLOCK || errno == EAGAIN || errno == EEXIST) return SECURITY_UPDATE_BUSY;
    if (errno == ENOTSUP) return SECURITY_UPDATE_UNSUPPORTED;
    if (errno == ELOOP || errno == EACCES || errno == EPERM || errno == ESTALE || errno == ENOTDIR)
        return SECURITY_UPDATE_UNSAFE_PATH;
    return SECURITY_UPDATE_IO_ERROR;
}
static void stage_name(const uint8_t id[16], char name[35]) { memcpy(name, "s-", 2); hex_encode(id, 16, name + 2); }
static bool stage_id(const char* name, uint8_t id[16]) {
    if (strlen(name) != 34 || strncmp(name, "s-", 2) || !hex_decode(name + 2, 32, id, nullptr)) return false;
    char canonical[35]; stage_name(id, canonical); return !strcmp(canonical, name);
}
static void stage_info(const StageIntent* record, SecurityUpdateStageInfo* info) {
    memset(info, 0, sizeof(*info)); memcpy(info->id, record->id, 16); memcpy(info->artifact, record->artifact, 32);
    memcpy(info->manifest, record->manifest, 32); info->entries = record->entries; info->bytes = record->bytes; info->ready = record->ready == 1;
}
static bool stage_intent_wire(SecurityStateWire* wire, StageIntent* r) {
    uint8_t magic[8] = {'L','M','D','S','T','G','0','1'}, expected[8]; memcpy(expected, magic, 8);
    uint32_t version = 1, reserved = 0;
    security_state_wire_bytes(wire, magic, 8); security_state_wire_u32(wire, &version); security_state_wire_u32(wire, &r->ready);
    security_state_wire_bytes(wire, r->id, 16); security_state_wire_bytes(wire, r->binding, 32);
    security_state_wire_bytes(wire, r->authority, 32); security_state_wire_bytes(wire, r->artifact, 32);
    security_state_wire_bytes(wire, r->description, 32); security_state_wire_bytes(wire, r->manifest, 32);
    security_state_wire_u64(wire, &r->device); security_state_wire_u64(wire, &r->inode);
    security_state_wire_u64(wire, &r->image_device); security_state_wire_u64(wire, &r->image_inode);
    security_state_wire_u64(wire, &r->bytes); security_state_wire_u32(wire, &r->entries); security_state_wire_u32(wire, &reserved);
    return wire->valid && !memcmp(magic, expected, 8) && version == 1 && !reserved && r->ready <= 2 &&
        r->device && r->inode && r->entries <= STAGE_ENTRIES && r->bytes <= UPDATE_WIRE_BODY_LIMIT &&
        security_state_fingerprint_present(r->binding) && security_state_fingerprint_present(r->authority) &&
        security_state_fingerprint_present(r->artifact) && security_state_fingerprint_present(r->description) &&
        (r->ready != 1 || (r->image_device && r->image_inode && security_state_fingerprint_present(r->manifest)));
}
static bool stage_intent_read(FileAnchor* directory, StageIntent* record, FileRevision* revision) {
    char* bytes = nullptr; size_t size = 0;
    if (!file_anchor_read_all(directory, "intent.bin", STAGE_INTENT_BYTES, MEM_CAT_SYSTEM, &bytes, &size, revision)) return false;
    uint8_t digest[32]; SecurityStateWire wire = {(uint8_t*)bytes, size, 0, false, true};
    bool valid = size == STAGE_INTENT_BYTES && digest_sha256(bytes, size - 32, digest) &&
        !memcmp(bytes + size - 32, digest, 32) && stage_intent_wire(&wire, record) && wire.offset == size - 32;
    mem_free(bytes);
    FileRevision held = file_anchor_identity(directory);
    if (!valid || held.device != record->device || held.inode != record->inode) { errno = ESTALE; return false; }
    return true;
}
static bool stage_intent_write(SecurityStagedUpdate* stage, bool create) {
    uint8_t bytes[STAGE_INTENT_BYTES] = {}; StageIntent record = stage->intent;
    SecurityStateWire wire = {bytes, sizeof(bytes), 0, true, true};
    if (!stage_intent_wire(&wire, &record) || wire.offset != sizeof(bytes) - 32 ||
        !digest_sha256(bytes, wire.offset, bytes + wire.offset)) { errno = EINVAL; return false; }
    int status = file_anchor_write_atomic(stage->directory, "intent.bin", bytes, sizeof(bytes), create ? nullptr : &stage->revision);
    if (status) { if (status == 1) errno = EIO; return false; }
    return file_anchor_stat(stage->directory, "intent.bin", &stage->revision);
}
static bool stage_claim(FileAnchor* directory, FILE** out, bool create) {
    *out = create ? file_anchor_create_regular(directory, ".lease") : file_anchor_open_read(directory, ".lease");
    if (!*out) return false;
#ifndef _WIN32
    if (flock(fileno(*out), LOCK_EX | LOCK_NB)) return false;
    FileRevision held = file_revision_stream(*out), named;
    if (file_anchor_stat(directory, ".lease", &named) && held.device == named.device && held.inode == named.inode) return true;
    errno = ESTALE;
#else
    errno = ENOTSUP;
#endif
    return false;
}
static void stage_close(SecurityStagedUpdate* stage) {
    if (!stage) return;
    if (stage->claim) fclose(stage->claim);
    file_anchor_close(stage->image); file_anchor_close(stage->directory); file_anchor_close(stage->root); mem_free(stage);
}
void security_update_stage_free(SecurityUpdateStageReport* report) {
    if (!report) return;
    stage_close(report->staged); confined_process_result_free(&report->process.process); memset(report, 0, sizeof(*report));
}
bool security_update_stage_path_allowed(const char* path) {
    if (!path) return false;
    size_t length = strnlen(path, 1025); if (!length || length > 1024) return false;
    uint32_t depth = 0;
    for (const char* part = path; *part;) {
        const char* end = strchr(part, '/'); if (!end) end = part + strlen(part);
        size_t size = (size_t)(end - part);
        if (!size || size > 255 || ++depth > 32 || part[0] == ' ' || end[-1] == ' ' || end[-1] == '.' ||
            (size == 1 && part[0] == '.') || (size == 2 && !memcmp(part, "..", 2))) return false;
        char lowered[256];
        for (size_t i = 0; i < size; i++) {
            unsigned char c = (unsigned char)part[i];
            if (c < 32 || c >= 127 || strchr("<>:\"\\|?*", c)) return false;
            lowered[i] = c >= 'A' && c <= 'Z' ? (char)(c + 32) : (char)c;
        }
        lowered[size] = 0;
        if (!strcmp(lowered, ".lambda") || !strncmp(lowered, FILE_ATOMIC_STAGE_PREFIX, strlen(FILE_ATOMIC_STAGE_PREFIX))) return false;
        char* dot = strchr(lowered, '.'); if (dot) *dot = 0;
        size_t stem = strlen(lowered); if (stem && lowered[stem - 1] == ' ') return false;
        const char* reserved[] = {"con", "conin$", "conout$", "prn", "aux", "nul"};
        for (const char* name : reserved) if (!strcmp(lowered, name)) return false;
        if (stem == 4 && (!memcmp(lowered, "com", 3) || !memcmp(lowered, "lpt", 3)) && lowered[3] >= '1' && lowered[3] <= '9') return false;
        if (!*end) return true;
        part = end + 1; if (!*part) return false;
    }
    return false;
}
static uint64_t stage_hash(const void* value, uint64_t a, uint64_t b) {
    StageEntry* entry = *(StageEntry* const*)value; return hashmap_sip(entry->key, strlen(entry->key), a, b);
}
static int stage_compare(const void* a, const void* b, void*) { return strcmp((*(StageEntry* const*)a)->key, (*(StageEntry* const*)b)->key); }
static StageEntry* stage_find(StageCall* call, char* key) {
    StageEntry probe = {}; probe.key = key; StageEntry* value = &probe;
    StageEntry* const* found = (StageEntry* const*)hashmap_get(call->names, &value); return found ? *found : nullptr;
}
static bool stage_check(void* pointer) {
    StageCall* call = (StageCall*)pointer; call->report->result = security_update_job_check(call->job);
    return call->report->result == SECURITY_UPDATE_OK;
}
static bool stage_begin(StageCall* call) {
    SecurityStateTransaction* state = nullptr;
    SecurityUpdateLease* lease = security_update_storage_enter(nullptr, call->job, &state, &call->report->result);
    if (!lease) return false;
    SecurityStagedUpdate* stage = (SecurityStagedUpdate*)mem_calloc(1, sizeof(*stage), MEM_CAT_SYSTEM);
    call->stage = stage;
    bool valid = stage && (stage->root = security_state_package_staging(state, true));
    ArrayList* entries = valid ? security_storage_entries(stage->root, 1) : nullptr;
    if (valid) valid = entries && !entries->length;
    if (entries && entries->length) errno = EEXIST;
    security_storage_entries_free(entries);
    if (valid) valid = crypto_random_bytes(stage->intent.id, 16);
    char name[35]; if (stage) stage_name(stage->intent.id, name);
    if (valid) valid = (stage->directory = file_anchor_create_directory(stage->root, name, FILE_ANCHOR_STRICT_STORAGE));
    if (valid) {
        call->report->created = true; memcpy(call->report->info.id, stage->intent.id, 16);
        valid = stage_claim(stage->directory, &stage->claim, true);
    }
    if (valid) {
        FileRevision revision = file_anchor_identity(stage->directory); stage->intent.device = revision.device; stage->intent.inode = revision.inode;
        memcpy(stage->intent.binding, call->binding, 32); memcpy(stage->intent.authority, call->authority, 32);
        memcpy(stage->intent.artifact, call->artifact, 32); memcpy(stage->intent.description, call->description, 32);
        valid = stage_intent_write(stage, true);
    }
    if (valid) valid = (stage->image = file_anchor_create_directory(stage->directory, "image", FILE_ANCHOR_STRICT_STORAGE));
    if (valid) {
        FileRevision revision = file_anchor_identity(stage->image); stage->intent.image_device = revision.device; stage->intent.image_inode = revision.inode;
        valid = stage_intent_write(stage, false) && !file_anchor_write_atomic(stage->directory, "description.json", call->input.description,
            call->input.description_size, nullptr);
    }
    if (!valid) call->report->result = stage_error();
    security_update_job_leave(lease); return valid;
}
static bool stage_entry(StageCall* call, UpdateWireMessage* m) {
    if (!security_update_stage_path_allowed(m->path) || call->entries->length >= STAGE_ENTRIES ||
        m->size > UPDATE_WIRE_STAGE_MEMBER_LIMIT || m->size > UPDATE_WIRE_BODY_LIMIT - call->bytes) return false;
    StageEntry* entry = (StageEntry*)mem_calloc(1, sizeof(*entry), MEM_CAT_SYSTEM);
    if (!entry) return false;
    entry->path = mem_strdup(m->path, MEM_CAT_SYSTEM); entry->key = mem_strdup(m->path, MEM_CAT_SYSTEM);
    entry->directory = m->artifact; entry->executable = m->status; entry->size = m->size;
    bool valid = entry->path && entry->key && digest_sha256(m->bytes, m->size, entry->digest);
    if (valid) {
        for (char* c = entry->key; *c; c++) if (*c >= 'A' && *c <= 'Z') *c += 32;
        valid = !stage_find(call, entry->key);
        char* slash = strrchr(entry->key, '/');
        if (valid && slash) { *slash = 0; StageEntry* parent = stage_find(call, entry->key); valid = parent && parent->directory; *slash = '/'; }
    }
    if (valid && !arraylist_append(call->entries, entry)) valid = false;
    if (!valid) { mem_free(entry->path); mem_free(entry->key); mem_free(entry); return false; }
    hashmap_set(call->names, &entry); if (hashmap_oom(call->names)) return false;
    SecurityUpdateLease* lease = security_update_job_enter(call->job, &call->report->result);
    if (!lease) return false;
    if (entry->directory) {
        char* slash = strrchr(entry->path, '/'); FileAnchor* parent = nullptr;
        if (slash) { *slash = 0; parent = file_anchor_directory(call->stage->image, entry->path, false, FILE_ANCHOR_STRICT_STORAGE); *slash = '/'; }
        FileAnchor* directory = (!slash || parent) ? file_anchor_create_directory(parent ? parent : call->stage->image,
            slash ? slash + 1 : entry->path, FILE_ANCHOR_STRICT_STORAGE) : nullptr;
        valid = directory != nullptr; file_anchor_close(directory); file_anchor_close(parent);
    } else {
        // Zero-byte IPC frames omit a buffer; the file API requires a present
        // empty source, distinct from a missing/invalid content pointer.
        const void* bytes = m->size ? (const void*)m->bytes : (const void*)"";
        valid = !file_anchor_write_atomic(call->stage->image, entry->path, bytes, m->size, nullptr);
    }
    if (!valid) call->report->result = stage_error();
    security_update_job_leave(lease);
    if (!valid) return false;
    // The host receipt uses explicit lengths/integers, never native struct padding.
    uint32_t flags = (entry->directory ? 1u : 0u) | (entry->executable ? 2u : 0u);
    size_t length = strlen(entry->path) + 1;
    size_t expected = call->manifest->length + 12 + length + 32;
    if (expected > STAGE_MANIFEST_BYTES || !strbuf_ensure_cap(call->manifest, expected + 1)) return false;
    SecurityStateWire wire = {(uint8_t*)call->manifest->str, expected, call->manifest->length, true, true};
    security_state_wire_u32(&wire, &flags); security_state_wire_u64(&wire, &entry->size);
    security_state_wire_bytes(&wire, entry->path, length); security_state_wire_bytes(&wire, entry->digest, 32);
    if (!wire.valid || wire.offset != expected) return false;
    call->manifest->length = expected; call->manifest->str[expected] = 0;
    call->bytes += entry->size; return true;
}
static bool stage_dispatch(UpdateWire* wire, void* pointer) {
    StageCall* call = (StageCall*)pointer;
    wire->check = stage_check; wire->context = call; wire->deadline = time_now_ms() + (uint64_t)call->seconds * 1000;
    if (!stage_check(call) || !stage_begin(call)) return false;
    UpdateWireMessage initial = {}; initial.kind = UPDATE_WIRE_STAGE_INIT; initial.limit = (uint64_t)call->seconds * 1000;
    initial.bytes = (uint8_t*)call->input.bytes; initial.size = (uint32_t)call->input.size;
    if (!security_update_wire_send(wire, &initial)) return false;
    for (uint32_t step = 0; step <= STAGE_ENTRIES; step++) {
        UpdateWireMessage m = {};
        if (!security_update_wire_receive(wire, update_wire_kind_mask(UPDATE_WIRE_STAGE_ENTRY) | update_wire_kind_mask(UPDATE_WIRE_STAGE_END), &m)) return false;
        struct MessageOwner { UpdateWireMessage* message; ~MessageOwner() { security_update_wire_free(message); } } owner = {&m};
        if (!stage_check(call)) return false;
        if (m.kind == UPDATE_WIRE_STAGE_ENTRY) {
            if (!stage_entry(call, &m)) return false;
            UpdateWireMessage ack = {}; ack.kind = UPDATE_WIRE_STAGE_ACK; ack.status = UPDATE_WIRE_OK;
            if (!security_update_wire_send(wire, &ack)) return false;
        } else {
            if (m.status != (uint32_t)call->entries->length || m.limit != call->bytes || memcmp(m.digest, call->artifact, 32)) return false;
            if (!digest_sha256(call->manifest->str, call->manifest->length, call->stage->intent.manifest)) return false;
            SecurityUpdateLease* lease = security_update_job_enter(call->job, &call->report->result);
            if (!lease) return false;
            bool valid = !file_anchor_write_atomic(call->stage->directory, "manifest.bin", call->manifest->str, call->manifest->length, nullptr);
            if (valid) {
                call->stage->intent.bytes = call->bytes; call->stage->intent.entries = (uint32_t)call->entries->length;
            }
            if (!valid) call->report->result = stage_error();
            security_update_job_leave(lease); return valid;
        }
    }
    return false;
}
static bool stage_prepare(StageCall* call, SecurityUpdateJob* job, SecurityVerifiedUpdate* artifact, SecurityUpdateStageReport* report) {
    if (!report) return false;
    memset(report, 0, sizeof(*report)); report->result = SECURITY_UPDATE_INVALID; report->process.process.exit_code = -1;
    *call = {}; call->job = job; call->report = report;
    SecurityUpdatePackageSpec spec = {};
    report->result = security_update_job_describe(job, &spec);
    if (report->result != SECURITY_UPDATE_OK || !security_update_verified_view(artifact, job, &call->input) ||
        !call->input.size || call->input.size > UPDATE_WIRE_BODY_LIMIT || !call->input.description_size ||
        call->input.description_size > STAGE_DESCRIPTION_BYTES || !security_update_job_binding(job, call->binding) ||
        !security_update_authority_digest(&spec, call->authority) || !digest_sha256(call->input.bytes, call->input.size, call->artifact) ||
        !digest_sha256(call->input.description, call->input.description_size, call->description)) return false;
    call->seconds = spec.job_seconds; call->entries = arraylist_new(0); call->manifest = strbuf_new();
    call->names = hashmap_new(sizeof(StageEntry*), 16, 0, 0, stage_hash, stage_compare, nullptr, nullptr);
    return call->entries && call->manifest && call->names;
}
static bool stage_finish(StageCall* call, bool success) {
    // Completion is published only after the process monitor has joined and
    // accepted a clean exit. A worker END followed by a crash is still inactive.
    if (success) {
        SecurityUpdateLease* lease = security_update_job_enter(call->job, &call->report->result);
        success = lease != nullptr;
        if (success) {
            call->stage->intent.ready = 1; success = stage_intent_write(call->stage, false);
            if (!success) call->report->result = stage_error();
            security_update_job_leave(lease);
        }
    }
    if (success) {
        stage_info(&call->stage->intent, &call->report->info); call->report->staged = call->stage; call->stage = nullptr;
    } else if (call->report->result == SECURITY_UPDATE_OK) {
        int error = call->report->process.system_error;
        call->report->result = error == EINVAL ? SECURITY_UPDATE_INVALID : error == ENOTSUP ? SECURITY_UPDATE_UNSUPPORTED :
            error ? SECURITY_UPDATE_IO_ERROR : SECURITY_UPDATE_CORRUPT;
    }
    if (call->entries) for (int i = 0; i < call->entries->length; i++) {
        StageEntry* e = (StageEntry*)call->entries->data[i]; mem_free(e->path); mem_free(e->key); mem_free(e);
    }
    arraylist_free(call->entries); if (call->names) hashmap_free(call->names); strbuf_free(call->manifest); stage_close(call->stage);
    return success;
}
bool security_update_stage(const SecurityUpdateStageConfig* config, SecurityUpdateJob* job, SecurityVerifiedUpdate* artifact, SecurityUpdateStageReport* report) {
    StageCall call = {}; bool success = stage_prepare(&call, job, artifact, report);
    if (success) {
        if (!config) { report->result = SECURITY_UPDATE_INVALID; success = false; }
        else success = security_update_process_run(&config->decoder, &config->limits, job,
            "lambda-package-stage-v1", stage_dispatch, &call, &report->process);
    }
    return report ? stage_finish(&call, success) : false;
}
bool security_update_staged_info(SecurityStagedUpdate* stage, SecurityUpdateJob* job, SecurityUpdateStageInfo* out) {
    if (!out) return false;
    memset(out, 0, sizeof(*out)); uint8_t binding[32]; StageIntent current = {}; FileRevision revision;
    if (!stage || stage->intent.ready != 1 || !security_update_job_binding(job, binding) || memcmp(binding, stage->intent.binding, 32)) return false;
    SecurityUpdateResult result; SecurityUpdateLease* lease = security_update_job_enter(job, &result);
    if (!lease) return false;
    char* manifest = nullptr; size_t size = 0; uint8_t digest[32];
    bool valid = file_anchor_validate(stage->image) && stage_intent_read(stage->directory, &current, &revision) &&
        file_revision_equal(&revision, &stage->revision) && !memcmp(current.manifest, stage->intent.manifest, 32) &&
        file_anchor_read_all(stage->directory, "manifest.bin", STAGE_MANIFEST_BYTES, MEM_CAT_SYSTEM, &manifest, &size, nullptr) &&
        digest_sha256(manifest, size, digest) && !memcmp(digest, current.manifest, 32);
    mem_free(manifest); if (valid) stage_info(&current, out);
    security_update_job_leave(lease); return valid;
}
struct StageRecovery {
    SecurityUpdateLease* lease;
    FileAnchor* root;
    FileAnchor* directory;
    FILE* claim;
    StageIntent intent;
    FileRevision revision;
    bool has_intent;
};
static void recovery_close(StageRecovery* r) {
    if (r->claim) fclose(r->claim);
    file_anchor_close(r->directory); file_anchor_close(r->root); security_update_job_leave(r->lease);
}
static bool recovery_orphan(FileAnchor* directory) {
    ArrayList* rows = security_storage_entries(directory, 2);
    if (!rows) return false;
    bool valid = true;
    for (int i = 0; i < rows->length; i++) {
        SecurityStorageEntry* row = (SecurityStorageEntry*)rows->data[i];
        valid = valid && row->revision.stat.is_file && row->revision.stat.size <= STAGE_INTENT_BYTES &&
            (!strcmp(row->name, ".lease") || file_path_has_atomic_stage(row->name));
    }
    security_storage_entries_free(rows); if (!valid) errno = ESTALE; return valid;
}
static SecurityUpdateResult recovery_open(SecurityUpdateControl* control, bool create, StageRecovery* r) {
    SecurityStateTransaction* state = nullptr; SecurityUpdateResult result;
    r->lease = security_update_storage_enter(control, nullptr, &state, &result);
    if (!r->lease) return result;
    r->root = security_state_package_staging(state, create);
    if (!r->root) return stage_error();
    ArrayList* rows = security_storage_entries(r->root, 1);
    if (!rows) return stage_error();
    if (!rows->length) { security_storage_entries_free(rows); return SECURITY_UPDATE_MISSING; }
    SecurityStorageEntry* row = (SecurityStorageEntry*)rows->data[0];
    uint8_t id[16];
    bool valid = row->revision.stat.is_dir && stage_id(row->name, id);
    if (valid) {
        r->directory = file_anchor_directory(r->root, row->name, false, FILE_ANCHOR_STRICT_STORAGE);
        valid = security_storage_same_object(r->directory, &row->revision);
    }
    security_storage_entries_free(rows);
    if (!valid) return SECURITY_UPDATE_UNSAFE_PATH;
    bool claimed = stage_claim(r->directory, &r->claim, false);
    if (!claimed && errno != ENOENT) return stage_error();
    if (stage_intent_read(r->directory, &r->intent, &r->revision)) {
        r->has_intent = true;
        if (!claimed || memcmp(id, r->intent.id, 16)) return SECURITY_UPDATE_UNSAFE_PATH;
    } else {
        if (errno != ENOENT || !recovery_orphan(r->directory)) return SECURITY_UPDATE_UNSAFE_PATH;
        memcpy(r->intent.id, id, 16);
    }
    return SECURITY_UPDATE_OK;
}
SecurityUpdateResult security_update_stage_inspect(SecurityUpdateControl* control, SecurityUpdateStageInfo* out) {
    if (!out) return SECURITY_UPDATE_INVALID;
    memset(out, 0, sizeof(*out)); StageRecovery r = {};
    SecurityUpdateResult result = recovery_open(control, false, &r);
    if (result == SECURITY_UPDATE_OK) stage_info(&r.intent, out);
    recovery_close(&r); return result;
}
static bool recovery_remove(FileAnchor* parent, const char* name, uint32_t* budget) {
    FileRevision revision;
    if (!file_anchor_stat(parent, name, &revision)) return errno == ENOENT;
    if (!*budget) return true;
    if (!file_anchor_remove(parent, name, &revision)) return false;
    --*budget; return true;
}
SecurityUpdateResult security_update_stage_discard(SecurityUpdateControl* control, const uint8_t id[16], uint32_t limit, bool* complete) {
    if (complete) *complete = false;
    if (!id || !complete || !limit || limit > 64) return SECURITY_UPDATE_INVALID;
    StageRecovery r = {}; SecurityUpdateResult result = recovery_open(control, true, &r);
    if (result == SECURITY_UPDATE_MISSING) { *complete = true; recovery_close(&r); return SECURITY_UPDATE_OK; }
    if (result != SECURITY_UPDATE_OK) { recovery_close(&r); return result; }
    if (memcmp(id, r.intent.id, 16)) { recovery_close(&r); return SECURITY_UPDATE_CONFLICT; }
    bool valid = true;
    if (r.has_intent && r.intent.ready != 2) {
        SecurityStagedUpdate stage = {}; stage.directory = r.directory; stage.intent = r.intent; stage.revision = r.revision;
        stage.intent.ready = 2; valid = stage_intent_write(&stage, false);
    }
    if (valid && r.has_intent) {
        FileRevision image;
        if (file_anchor_stat(r.directory, "image", &image)) {
            FileAnchor* directory = file_anchor_directory(r.directory, "image", false, FILE_ANCHOR_STRICT_STORAGE);
            valid = security_storage_same_object(directory, &image);
            if (valid && r.intent.image_inode && (image.device != r.intent.image_device || image.inode != r.intent.image_inode)) {
                errno = ESTALE; valid = false;
            }
            if (valid && !r.intent.image_inode) {
                ArrayList* rows = security_storage_entries(directory, 1); valid = rows && !rows->length; security_storage_entries_free(rows);
            }
            uint32_t entries = STAGE_TREE_ENTRIES; bool more = false;
            if (valid) valid = security_storage_clear(directory, &entries, 0, 34, &limit, &more);
            file_anchor_close(directory);
            if (valid && !more && limit) valid = recovery_remove(r.directory, "image", &limit);
            if (valid && (more || !limit)) { recovery_close(&r); return SECURITY_UPDATE_OK; }
        } else valid = errno == ENOENT;
    }
    ArrayList* rows = valid ? security_storage_entries(r.directory, 8) : nullptr;
    if (valid) valid = rows != nullptr;
    // Intent survives all payload cleanup; an interrupted step remains resumable.
    for (int i = 0; valid && rows && i < rows->length; i++) {
        SecurityStorageEntry* row = (SecurityStorageEntry*)rows->data[i];
        if (!strcmp(row->name, "intent.bin") || !strcmp(row->name, ".lease")) continue;
        valid = row->revision.stat.is_file && (!strcmp(row->name, "manifest.bin") || !strcmp(row->name, "description.json") ||
            file_path_has_atomic_stage(row->name));
        if (!valid) { errno = ESTALE; break; }
        if (!limit) { security_storage_entries_free(rows); recovery_close(&r); return SECURITY_UPDATE_OK; }
        valid = recovery_remove(r.directory, row->name, &limit);
    }
    security_storage_entries_free(rows);
    if (valid && limit) valid = recovery_remove(r.directory, "intent.bin", &limit);
    if (valid && limit) valid = recovery_remove(r.directory, ".lease", &limit);
    if (valid && limit) {
        char name[35]; stage_name(id, name); FileRevision revision = file_anchor_identity(r.directory);
        valid = file_anchor_remove(r.root, name, &revision); if (valid) *complete = true;
    }
    result = valid ? SECURITY_UPDATE_OK : stage_error(); recovery_close(&r); return result;
}
#ifdef LAMBDA_SECURITY_UPDATE_TESTING
bool security_update_stage_fixture(int fd, uint64_t session, SecurityUpdateJob* job, SecurityVerifiedUpdate* artifact, SecurityUpdateStageReport* report) {
    StageCall call = {}; bool success = stage_prepare(&call, job, artifact, report);
    IpcStream* stream = ipc_stream_adopt(fd); if (!stream) confined_process_close_channel(fd);
    if (success && stream) {
        UpdateWire wire = {stream, session, 0, 0, time_now_ms() + 5000, nullptr, nullptr}; UpdateWireMessage ready = {};
        success = security_update_wire_receive(&wire, update_wire_kind_mask(UPDATE_WIRE_READY), &ready) && stage_dispatch(&wire, &call);
        security_update_wire_free(&ready);
    } else success = false;
    ipc_stream_close(stream); return report ? stage_finish(&call, success) : false;
}
#endif
