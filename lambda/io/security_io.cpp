#include "security_io.h"
#include "../../lib/file.h"
#include "../../lib/file_utils.h"
#include "../../lib/arraylist.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

void security_io_directory_free(SecurityDirectory* directory) {
    if (!directory) return;
    for (uint32_t i = 0; i < directory->count; i++) mem_free(directory->entries[i].name);
    mem_free(directory->entries); *directory = {};
}

bool security_io_authority_stamp(const SecurityIOClient* client,
        SecurityAuthorityStamp* stamp, SecurityIOResult* result) {
    if (stamp) *stamp = {};
    SecurityIOResult status = {SECURITY_DENIED, EACCES, false, {}};
    if (client && client->version == 1 && client->authority_stamp && stamp)
        status = client->authority_stamp(client->context, stamp);
    if (status.decision == SECURITY_ALLOWED && !status.system_error &&
            (!stamp || !stamp->session || !stamp->epoch)) status.system_error = EINVAL;
    bool ok = status.decision == SECURITY_ALLOWED && !status.system_error &&
        stamp && stamp->session && stamp->epoch;
    if (!ok && stamp) *stamp = {};
    if (result) *result = status;
    return ok;
}

bool security_io_stat(const SecurityIOClient* client, const char* path,
        FileStat* metadata, SecurityIOResult* result) {
    if (metadata) *metadata = {};
    SecurityIOResult status = {SECURITY_DENIED, EACCES, false, {}};
    if (path && metadata) {
        if (!client) {
            *metadata = file_stat(path);
            status = {SECURITY_ALLOWED, 0, false, {}};
        } else if (client->version == 1 && client->stat)
            status = client->stat(client->context, path, metadata);
    }
    bool ok = status.decision == SECURITY_ALLOWED && !status.system_error;
    if (!ok && metadata) *metadata = {};
    if (result) *result = status;
    return ok;
}

static SecurityIOResult security_io_ambient_directory(const char* path,
        uint32_t max_entries, SecurityDirectory* directory) {
    ArrayList* entries = dir_list(path);
    if (!entries) return {SECURITY_ALLOWED, errno ? errno : EIO, false, {}};
    SecurityIOResult status = {SECURITY_ALLOWED, 0, false, {}};
    if ((uint64_t)entries->length > max_entries) status.system_error = EFBIG;
    else {
        directory->entries = (SecurityDirectoryEntry*)mem_calloc(entries->length,
            sizeof(SecurityDirectoryEntry), MEM_CAT_INPUT_OTHER);
        if (entries->length && !directory->entries) status.system_error = ENOMEM;
    }
    for (int i = 0; i < entries->length; i++) {
        DirEntry* entry = (DirEntry*)entries->data[i];
        if (!status.system_error) {
            SecurityDirectoryEntry* item = &directory->entries[directory->count++];
            item->name = mem_strdup(entry->name, MEM_CAT_INPUT_OTHER);
            char* full = file_path_join(path, entry->name);
            item->metadata = full ? file_stat(full) : FileStat{};
            item->has_metadata = item->metadata.exists;
            item->is_directory = entry->is_dir;
            mem_free(full);
            if (!item->name) status.system_error = ENOMEM;
        }
        dir_entry_free(entry);
    }
    arraylist_free(entries);
    return status;
}

static int security_io_entry_compare(const void* a, const void* b) {
    return strcmp(((const SecurityDirectoryEntry*)a)->name, ((const SecurityDirectoryEntry*)b)->name);
}

bool security_io_input_kind(const SecurityIOClient* client, const char* path,
        int32_t* kind, SecurityIOResult* result) {
    if (kind) *kind = -1;
    SecurityIOResult status = {SECURITY_DENIED, EACCES, false, {}};
    if (client && client->version == 1 && client->input_kind && path && kind)
        status = client->input_kind(client->context, path, kind);
    else if (!client && path && kind) {
        FileStat metadata = file_stat(path);
        status = {SECURITY_ALLOWED, metadata.exists ? 0 : ENOENT, false, {}};
        if (metadata.is_dir) *kind = SECURITY_OBJECT_DIRECTORY;
        else if (metadata.is_file) *kind = SECURITY_OBJECT_FILE;
        else if (metadata.exists) status.system_error = EACCES;
    }
    bool ok = status.decision == SECURITY_ALLOWED && !status.system_error;
    if (ok && *kind != SECURITY_OBJECT_FILE && *kind != SECURITY_OBJECT_DIRECTORY) {
        status.system_error = EINVAL; ok = false;
    }
    if (!ok && kind) *kind = -1;
    if (result) *result = status;
    return ok;
}

bool security_io_enumerate(const SecurityIOClient* client, const char* path,
        uint32_t max_entries, SecurityDirectory* directory, SecurityIOResult* result) {
    if (directory) *directory = {};
    SecurityIOResult status = {SECURITY_DENIED, EACCES, false, {}};
    if (client && client->version == 1 && client->enumerate && path && directory &&
            max_entries && max_entries <= SECURITY_IO_DIRECTORY_LIMIT)
        status = client->enumerate(client->context, path, max_entries, directory);
    else if (!client && path && directory && max_entries)
        status = security_io_ambient_directory(path, max_entries, directory);
    bool ok = status.decision == SECURITY_ALLOWED && !status.system_error;
    if (ok && (directory->count > max_entries || (directory->count && !directory->entries))) {
        status.system_error = EFBIG; ok = false;
    }
    for (uint32_t i = 0; ok && i < directory->count; i++) {
        if (!directory->entries[i].name) { status.system_error = EINVAL; ok = false; }
    }
    // Match the trusted dir_list contract: stable bytewise name order, including
    // parser include directories whose declaration order can affect results.
    if (ok && client && directory->count > 1)
        qsort(directory->entries, directory->count, sizeof(SecurityDirectoryEntry), security_io_entry_compare);
    if (!ok) security_io_directory_free(directory);
    if (result) *result = status;
    return ok;
}

bool security_io_read_bytes(const SecurityIOClient* client, const char* path,
        bool source, uint64_t limit, MemCategory category, char** data, size_t* size,
        SecurityIOResult* result) {
    if (data) *data = nullptr;
    if (size) *size = 0;
    SecurityIOResult status = {SECURITY_INVALID_REQUEST, EINVAL, false, {}};
    if (data && size && path) {
        if (client) {
            status = {SECURITY_DENIED, EACCES, false, {}};
            if (client->version == 1 && client->read)
                status = client->read(client->context, path, source, limit, category, data, size);
        } else {
            status = {SECURITY_ALLOWED, 0, false, {}};
            if (!file_read_all_limit(path, category, limit, data, size)) status.system_error = errno;
        }
        if (status.decision == SECURITY_ALLOWED && !status.system_error &&
            (!*data || *size > limit)) {
            status.system_error = *size > limit ? EFBIG : EIO;
        }
    }
    bool ok = status.decision == SECURITY_ALLOWED && !status.system_error;
    if (!ok) {
        if (data) { mem_free(*data); *data = nullptr; }
        if (size) *size = 0;
    }
    if (result) *result = status;
    return ok;
}

bool security_io_source_path(const SecurityIOClient* client, const char* path,
        char** canonical, SecurityIOResult* result) {
    if (canonical) *canonical = nullptr;
    SecurityIOResult status = {SECURITY_DENIED, EACCES, false, {}};
    if (client && client->version == 1 && client->canonical_source && path && canonical)
        status = client->canonical_source(client->context, path, canonical);
    bool ok = status.decision == SECURITY_ALLOWED && !status.system_error;
    if (ok && (!canonical || !*canonical || !**canonical || strnlen(*canonical, 4096) >= 4096)) {
        status.system_error = EINVAL; ok = false;
    }
    if (!ok && canonical) { mem_free(*canonical); *canonical = nullptr; }
    if (result) *result = status;
    return ok;
}
