#pragma once
#include "../../../lib/file.h"
#include "../../../lib/arraylist.h"

struct SecurityStorageEntry { FileRevision revision; char name[256]; };
ArrayList* security_storage_entries(FileAnchor* directory, uint32_t limit);
void security_storage_entries_free(ArrayList* entries);
bool security_storage_same_object(FileAnchor* anchor, const FileRevision* revision);
// Bounded strict anchored cleanup; links/special objects fail, never disappear
// from accounting. A partial step preserves its remaining intent and identity.
bool security_storage_clear(FileAnchor* directory, uint32_t* remaining, uint32_t depth,
    uint32_t depth_limit, uint32_t* budget, bool* more);
