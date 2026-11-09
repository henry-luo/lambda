#include "storage_tree.h"
#include "lib/mem.h"
#include <errno.h>
#include <string.h>

void security_storage_entries_free(ArrayList* entries) {
    if (!entries) return;
    for (int i = 0; i < entries->length; i++) mem_free(entries->data[i]);
    arraylist_free(entries);
}
ArrayList* security_storage_entries(FileAnchor* directory, uint32_t limit) {
    ArrayList* entries = arraylist_new(0);
    auto capture = [](void* context, const char* name, const FileRevision* revision) -> bool {
        if (strlen(name) >= sizeof(SecurityStorageEntry::name)) { errno = ENAMETOOLONG; return false; }
        SecurityStorageEntry* entry = (SecurityStorageEntry*)mem_alloc(sizeof(*entry), MEM_CAT_SYSTEM);
        if (!entry) { errno = ENOMEM; return false; }
        entry->revision = *revision; memcpy(entry->name, name, strlen(name) + 1);
        if (arraylist_append((ArrayList*)context, entry)) return true;
        mem_free(entry); errno = ENOMEM; return false;
    };
    // Enumeration revalidates before returning; deletion cannot run in its visitor.
    if (entries && file_anchor_visit_strict(directory, limit, capture, entries)) return entries;
    int error = errno; security_storage_entries_free(entries); errno = error; return nullptr;
}
bool security_storage_same_object(FileAnchor* anchor, const FileRevision* revision) {
    FileRevision held = file_anchor_identity(anchor);
    if (anchor && held.device == revision->device && held.inode == revision->inode) return true;
    errno = ESTALE; return false;
}
bool security_storage_clear(FileAnchor* directory, uint32_t* remaining, uint32_t depth,
        uint32_t depth_limit, uint32_t* budget, bool* more) {
    if (depth > depth_limit) { errno = EFBIG; return false; }
    ArrayList* entries = security_storage_entries(directory, *remaining ? *remaining : 1);
    if (!entries) return false;
    bool valid = true;
    for (int i = 0; valid && i < entries->length; i++) {
        if (budget && !*budget) { *more = true; break; }
        SecurityStorageEntry* entry = (SecurityStorageEntry*)entries->data[i];
        if (!*remaining) { errno = EFBIG; valid = false; break; }
        --*remaining;
        if (entry->revision.stat.is_dir) {
            FileAnchor* child = file_anchor_directory(directory, entry->name, false, FILE_ANCHOR_STRICT_STORAGE);
            valid = security_storage_same_object(child, &entry->revision) && security_storage_clear(child, remaining, depth + 1, depth_limit, budget, more);
            file_anchor_close(child);
            // A bounded step may empty the child without having budget to
            // unlink its directory. Keep the durable intent for the next step.
            if (valid && budget && (*more || !*budget)) { *more = true; break; }
        }
        if (valid) {
            valid = file_anchor_remove(directory, entry->name, &entry->revision);
            if (valid && budget) --*budget;
        }
    }
    int error = errno; security_storage_entries_free(entries); errno = error; return valid;
}
