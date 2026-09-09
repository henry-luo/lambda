#include "async.h"

#include "transpiler.hpp"
#include "lambda-root-frame.hpp"

enum { RUNTIME_JOB_ITEM_COUNT = 7 };

enum RuntimeJobItemSlot {
    RUNTIME_JOB_SLOT_CALLBACK = 0,
    RUNTIME_JOB_SLOT_ARGUMENTS,
    RUNTIME_JOB_SLOT_RESOURCE,
    RUNTIME_JOB_SLOT_ALS_CONTEXT,
    RUNTIME_JOB_SLOT_DOMAIN,
    RUNTIME_JOB_SLOT_ID,
    RUNTIME_JOB_SLOT_KIND,
};

static Array* runtime_job_queue_storage(const RuntimeJobQueue* queue) {
    if (!queue || !queue->storage_owner) return NULL;
    Item storage = *queue->storage_owner;
    return get_type_id(storage) == LMD_TYPE_ARRAY ? storage.array : NULL;
}

static void runtime_job_encode(Item* slots, const RuntimeJob* job) {
    slots[RUNTIME_JOB_SLOT_CALLBACK] = job->callback;
    slots[RUNTIME_JOB_SLOT_ARGUMENTS] = job->arguments;
    slots[RUNTIME_JOB_SLOT_RESOURCE] = job->context.resource;
    slots[RUNTIME_JOB_SLOT_ALS_CONTEXT] = job->context.als_context;
    slots[RUNTIME_JOB_SLOT_DOMAIN] = job->context.domain;
    slots[RUNTIME_JOB_SLOT_ID] = (Item){.item = i2it(job->id)};
    slots[RUNTIME_JOB_SLOT_KIND] = (Item){.item = i2it((int64_t)job->kind)};
}

static RuntimeJobKind runtime_job_decode(Item* slots, RuntimeJob* job) {
    job->callback = slots[RUNTIME_JOB_SLOT_CALLBACK];
    job->arguments = slots[RUNTIME_JOB_SLOT_ARGUMENTS];
    job->context.resource = slots[RUNTIME_JOB_SLOT_RESOURCE];
    job->context.als_context = slots[RUNTIME_JOB_SLOT_ALS_CONTEXT];
    job->context.domain = slots[RUNTIME_JOB_SLOT_DOMAIN];
    job->id = it2i(slots[RUNTIME_JOB_SLOT_ID]);
    job->kind = (RuntimeJobKind)it2i(slots[RUNTIME_JOB_SLOT_KIND]);
    return job->kind;
}

extern "C" void runtime_job_queue_init(RuntimeJobQueue* queue,
        Item* storage_owner) {
    if (!queue) return;
    queue->storage_owner = storage_owner;
    queue->head = 0;
    queue->pending_count = 0;
}

extern "C" int64_t runtime_job_queue_size(const RuntimeJobQueue* queue) {
    return queue ? queue->pending_count : 0;
}

extern "C" bool runtime_job_queue_push(RuntimeJobQueue* queue,
        const RuntimeJob* job) {
    if (!queue || !queue->storage_owner || !job || job->kind == RUNTIME_JOB_NONE) return false;
    Item* owner = queue->storage_owner;
    if (get_type_id(*owner) != LMD_TYPE_ARRAY) {
        Item storage = {.array = array()};
        if (!storage.array) return false;
        *owner = storage;
    }

    Array* array_ptr = owner->array;
    int64_t required = array_ptr->length + RUNTIME_JOB_ITEM_COUNT;
    while (required > array_ptr->capacity) {
        int64_t old_capacity = array_ptr->capacity;
        expand_list((List*)array_ptr, NULL);
        array_ptr = owner->array;
        if (!array_ptr || array_ptr->capacity <= old_capacity) return false;
    }
    runtime_job_encode(array_ptr->items + array_ptr->length, job);
    array_ptr->length += RUNTIME_JOB_ITEM_COUNT;
    queue->pending_count++;
    return true;
}

extern "C" bool runtime_job_queue_pop(RuntimeJobQueue* queue,
        RuntimeJob* job) {
    if (!queue || !queue->storage_owner || !job) return false;
    Array* array_ptr = runtime_job_queue_storage(queue);
    while (array_ptr && queue->head + RUNTIME_JOB_ITEM_COUNT <= array_ptr->length) {
        Item* slots = array_ptr->items + queue->head;
        RuntimeJobKind kind = runtime_job_decode(slots, job);
        for (int i = 0; i < RUNTIME_JOB_ITEM_COUNT; i++) slots[i] = ItemNull;
        queue->head += RUNTIME_JOB_ITEM_COUNT;
        if (kind != RUNTIME_JOB_NONE) {
            if (queue->pending_count > 0) queue->pending_count--;
            if (queue->head == array_ptr->length) {
                *queue->storage_owner = ItemNull;
                queue->head = 0;
            }
            return true;
        }
        if (queue->head == array_ptr->length) {
            *queue->storage_owner = ItemNull;
            queue->head = 0;
            break;
        }
    }
    return false;
}

extern "C" bool runtime_job_queue_cancel(RuntimeJobQueue* queue, int64_t id) {
    if (!queue || !queue->storage_owner) return false;
    Array* array_ptr = runtime_job_queue_storage(queue);
    if (!array_ptr) return false;
    for (int64_t offset = queue->head;
            offset + RUNTIME_JOB_ITEM_COUNT <= array_ptr->length;
            offset += RUNTIME_JOB_ITEM_COUNT) {
        Item* slots = array_ptr->items + offset;
        if (it2i(slots[RUNTIME_JOB_SLOT_KIND]) == RUNTIME_JOB_NONE ||
                it2i(slots[RUNTIME_JOB_SLOT_ID]) != id) continue;
        for (int i = 0; i < RUNTIME_JOB_ITEM_COUNT; i++) slots[i] = ItemNull;
        if (queue->pending_count > 0) queue->pending_count--;
        return true;
    }
    return false;
}

extern "C" void runtime_job_queue_clear(RuntimeJobQueue* queue) {
    if (!queue || !queue->storage_owner) return;
    *queue->storage_owner = ItemNull;
    queue->head = 0;
    queue->pending_count = 0;
}

extern "C" void runtime_callback_slots_init(RuntimeCallbackSlots* slots,
        Context* owner, const char* name) {
    if (!slots) return;
    root_vector_init(&slots->values, owner, name);
}

extern "C" bool runtime_callback_slots_add(RuntimeCallbackSlots* slots,
        Item callback, int64_t* out_slot) {
    if (!slots || !out_slot) return false;
    *out_slot = -1;
    RootFrame roots(1);
    Rooted<Item> callback_root(roots, callback);
    int64_t slot = root_vector_count(&slots->values);
    if (!root_vector_push(&slots->values, callback_root.get())) return false;
    *out_slot = slot;
    return true;
}

extern "C" Item runtime_callback_slots_take(RuntimeCallbackSlots* slots,
        int64_t slot) {
    if (!slots || slot < 0) return ItemNull;
    Item* value = root_vector_at(&slots->values, slot);
    Item callback = value ? *value : ItemNull;
    if (value) *value = ItemNull;
    // Tail shrinking retains stable indices for every completion still held
    // by libuv while releasing historical callback storage promptly.
    for (;;) {
        int64_t count = root_vector_count(&slots->values);
        if (count <= 0) break;
        Item* last = root_vector_at(&slots->values, count - 1);
        if (!last || last->item != ItemNull.item) break;
        root_vector_pop(&slots->values);
    }
    return callback;
}

extern "C" void runtime_callback_slots_destroy(RuntimeCallbackSlots* slots) {
    if (!slots) return;
    root_vector_destroy(&slots->values);
}

extern "C" bool runtime_value_slots_init(RuntimeValueSlots* slots,
        Context* owner, const char* name, int count) {
    if (!slots || count < 0) return false;
    root_vector_init(&slots->values, owner, name);
    for (int i = 0; i < count; i++) {
        // Fixed native fields historically use the all-zero Item as their
        // absent sentinel; preserve that contract across the shared carrier.
        if (!root_vector_push(&slots->values, (Item){0})) {
            root_vector_destroy(&slots->values);
            return false;
        }
    }
    return true;
}

extern "C" Item runtime_value_slots_get(RuntimeValueSlots* slots, int slot) {
    Item* value = slots && slot >= 0 ? root_vector_at(&slots->values, slot) : NULL;
    return value ? *value : (Item){0};
}

extern "C" void runtime_value_slots_set(RuntimeValueSlots* slots, int slot,
        Item value) {
    Item* target = slots && slot >= 0 ? root_vector_at(&slots->values, slot) : NULL;
    if (target) *target = value;
}

extern "C" void runtime_value_slots_destroy(RuntimeValueSlots* slots) {
    if (!slots) return;
    root_vector_destroy(&slots->values);
}

enum {
    RUNTIME_RESOURCE_INDEX_BITS = 16,
    RUNTIME_RESOURCE_INDEX_MASK = (1u << RUNTIME_RESOURCE_INDEX_BITS) - 1u,
};

static const RuntimeResourceDescriptor runtime_resource_descriptors[] = {
    {RUNTIME_RESOURCE_TIMER, RUNTIME_RESOURCE_GROUP_NONE, "timer"},
    {RUNTIME_RESOURCE_TCP_SOCKET, RUNTIME_RESOURCE_GROUP_NETWORK, "TCPSocketWrap"},
    {RUNTIME_RESOURCE_TCP_SERVER, RUNTIME_RESOURCE_GROUP_NETWORK, "TCPServerWrap"},
    {RUNTIME_RESOURCE_CRYPTO_HMAC, RUNTIME_RESOURCE_GROUP_CRYPTO, "crypto.hmac"},
    {RUNTIME_RESOURCE_CRYPTO_HASH, RUNTIME_RESOURCE_GROUP_CRYPTO, "crypto.hash"},
    {RUNTIME_RESOURCE_CRYPTO_SIGN, RUNTIME_RESOURCE_GROUP_CRYPTO, "crypto.sign"},
    {RUNTIME_RESOURCE_CRYPTO_CIPHER, RUNTIME_RESOURCE_GROUP_CRYPTO, "crypto.cipher"},
    {RUNTIME_RESOURCE_FS_REQUEST, RUNTIME_RESOURCE_GROUP_NONE, "FSReqCallback"},
    {RUNTIME_RESOURCE_DNS_REQUEST, RUNTIME_RESOURCE_GROUP_NETWORK, "DNSReqCallback"},
};

extern "C" const RuntimeResourceDescriptor*
runtime_resource_descriptor_from_legacy_name(const char* name) {
    if (!name) return NULL;
    for (size_t i = 0;
            i < sizeof(runtime_resource_descriptors) /
                sizeof(runtime_resource_descriptors[0]);
            i++) {
        const RuntimeResourceDescriptor* descriptor = &runtime_resource_descriptors[i];
        if (strcmp(descriptor->display_name, name) == 0) return descriptor;
    }
    return NULL;
}

extern "C" void runtime_resource_table_init(RuntimeResourceTable* table,
        Context* owner, const char* name) {
    if (!table) return;
    memset(table, 0, sizeof(*table));
    root_vector_init(&table->owner_values, owner, name);
}

static RuntimeResourceSlot* runtime_resource_table_slot(
        RuntimeResourceTable* table, uint32_t id) {
    if (!table || !table->slots || id == 0) return NULL;
    uint32_t index = id & RUNTIME_RESOURCE_INDEX_MASK;
    uint16_t generation = (uint16_t)(id >> RUNTIME_RESOURCE_INDEX_BITS);
    if (index == 0 || generation == 0 || index > (uint32_t)table->slots->length) {
        return NULL;
    }
    RuntimeResourceSlot* slot = (RuntimeResourceSlot*)
        table->slots->data[index - 1];
    return slot && slot->generation == generation && slot->entry &&
        !slot->entry->closing ? slot : NULL;
}

extern "C" const RuntimeResourceEntry* runtime_resource_table_entry(
        RuntimeResourceTable* table, uint32_t id) {
    RuntimeResourceSlot* slot = runtime_resource_table_slot(table, id);
    return slot ? slot->entry : NULL;
}

extern "C" const RuntimeResourceEntry* runtime_resource_table_entry_owned(
        RuntimeResourceTable* table, void* lifecycle_owner, uint32_t id) {
    const RuntimeResourceEntry* entry = runtime_resource_table_entry(table, id);
    return entry && entry->lifecycle_owner == lifecycle_owner ? entry : NULL;
}

extern "C" int runtime_resource_table_slot_count(
        const RuntimeResourceTable* table) {
    return table && table->slots ? table->slots->length : 0;
}

extern "C" int runtime_resource_table_active_count(
        const RuntimeResourceTable* table) {
    return table ? table->active_count : 0;
}

extern "C" int runtime_resource_table_active_count_owned(
        const RuntimeResourceTable* table, void* lifecycle_owner) {
    if (!table || !table->slots) return 0;
    int count = 0;
    for (int i = 0; i < table->slots->length; i++) {
        RuntimeResourceSlot* slot = (RuntimeResourceSlot*)table->slots->data[i];
        if (slot && slot->entry && !slot->entry->closing &&
                slot->entry->lifecycle_owner == lifecycle_owner) {
            count++;
        }
    }
    return count;
}

extern "C" const RuntimeResourceEntry* runtime_resource_table_entry_at(
        RuntimeResourceTable* table, int index) {
    if (!table || !table->slots || index < 0 || index >= table->slots->length) {
        return NULL;
    }
    RuntimeResourceSlot* slot = (RuntimeResourceSlot*)table->slots->data[index];
    return slot && slot->entry && !slot->entry->closing ? slot->entry : NULL;
}

extern "C" Item runtime_resource_table_value(RuntimeResourceTable* table,
        const RuntimeResourceEntry* entry) {
    if (!table || !entry) return ItemNull;
    Item* value = root_vector_at(&table->owner_values, entry->root_slot);
    return value ? *value : ItemNull;
}

extern "C" void* runtime_resource_table_user_data(RuntimeResourceTable* table,
        uint32_t id) {
    const RuntimeResourceEntry* entry = runtime_resource_table_entry(table, id);
    return entry ? entry->close_user : NULL;
}

extern "C" void* runtime_resource_table_user_data_owned(
        RuntimeResourceTable* table, void* lifecycle_owner, uint32_t id) {
    const RuntimeResourceEntry* entry = runtime_resource_table_entry_owned(table,
        lifecycle_owner, id);
    return entry ? entry->close_user : NULL;
}

static void runtime_resource_table_release_entry(RuntimeResourceTable* table,
        RuntimeResourceSlot* slot, bool close_native) {
    if (!table || !slot || !slot->entry) return;
    RuntimeResourceEntry* entry = slot->entry;
    entry->closing = true;
    // Invalidate the rid before protocol teardown: close callbacks can reenter
    // the table, but must not acquire a newly closing native payload.
    slot->entry = NULL;
    if (table->active_count > 0) table->active_count--;
    if (close_native && entry->close_callback) entry->close_callback(entry->close_user);
    for (int i = 0; i < entry->root_count; i++) {
        Item* value = root_vector_at(&table->owner_values,
            entry->root_slot + i);
        if (value) *value = ItemNull;
    }
    mem_free(entry);
}

extern "C" void runtime_resource_table_remove(RuntimeResourceTable* table,
        uint32_t id) {
    RuntimeResourceSlot* slot = runtime_resource_table_slot(table, id);
    runtime_resource_table_release_entry(table, slot, true);
}

extern "C" void runtime_resource_table_remove_owned(RuntimeResourceTable* table,
        void* lifecycle_owner, uint32_t id) {
    RuntimeResourceSlot* slot = runtime_resource_table_slot(table, id);
    if (!slot || !slot->entry || slot->entry->lifecycle_owner != lifecycle_owner) return;
    runtime_resource_table_release_entry(table, slot, true);
}

extern "C" void runtime_resource_table_forget(RuntimeResourceTable* table,
        uint32_t id) {
    RuntimeResourceSlot* slot = runtime_resource_table_slot(table, id);
    runtime_resource_table_release_entry(table, slot, false);
}

extern "C" void runtime_resource_table_forget_owned(RuntimeResourceTable* table,
        void* lifecycle_owner, uint32_t id) {
    RuntimeResourceSlot* slot = runtime_resource_table_slot(table, id);
    if (!slot || !slot->entry || slot->entry->lifecycle_owner != lifecycle_owner) return;
    runtime_resource_table_release_entry(table, slot, false);
}

extern "C" void runtime_resource_table_clear_owned(RuntimeResourceTable* table,
        void* lifecycle_owner) {
    if (!table || !table->slots) return;
    for (int i = 0; i < table->slots->length; i++) {
        RuntimeResourceSlot* slot = (RuntimeResourceSlot*)table->slots->data[i];
        if (slot && slot->entry && slot->entry->lifecycle_owner == lifecycle_owner) {
            runtime_resource_table_release_entry(table, slot, true);
        }
    }
}

extern "C" void runtime_resource_table_clear(RuntimeResourceTable* table) {
    if (!table) return;
    if (table->slots) {
        for (int i = 0; i < table->slots->length; i++) {
            RuntimeResourceSlot* slot = (RuntimeResourceSlot*)table->slots->data[i];
            runtime_resource_table_release_entry(table, slot, true);
            if (slot) mem_free(slot);
        }
        arraylist_free(table->slots);
        table->slots = NULL;
    }
    root_vector_clear(&table->owner_values);
}

extern "C" void runtime_resource_table_destroy(RuntimeResourceTable* table) {
    if (!table) return;
    runtime_resource_table_clear(table);
    root_vector_destroy(&table->owner_values);
}

extern "C" uint32_t runtime_resource_table_add_root_span_owned(
        RuntimeResourceTable* table, void* lifecycle_owner,
        const Item* root_values, int root_count,
        const RuntimeResourceDescriptor* descriptor,
    RuntimeResourceCloseCallback close_callback, void* close_user,
    bool is_handle) {
    if (!table || !root_values || root_count <= 0 || !root_values[0].item ||
            !descriptor) return 0;
    RootFrame roots((size_t)root_count);
    for (int i = 0; i < root_count; i++) {
        uint64_t* root = roots.slot((size_t)i);
        if (!root) return 0;
        *root = root_values[i].item;
    }
    Item value = (Item){.item = *roots.slot(0)};
    if (!table->slots) {
        table->slots = arraylist_new(8);
        if (!table->slots) return 0;
    }
    for (int i = 0; i < table->slots->length; i++) {
        RuntimeResourceSlot* slot = (RuntimeResourceSlot*)table->slots->data[i];
        RuntimeResourceEntry* entry = slot ? slot->entry : NULL;
        if (entry && entry->lifecycle_owner == lifecycle_owner &&
                runtime_resource_table_value(table, entry).item == value.item) {
            // A script owner has exactly one native close authority.
            return close_callback ? 0 : entry->id;
        }
    }

    RuntimeResourceSlot* slot = NULL;
    int index = -1;
    for (int i = 0; i < table->slots->length; i++) {
        RuntimeResourceSlot* candidate = (RuntimeResourceSlot*)table->slots->data[i];
        if (candidate && !candidate->entry) {
            slot = candidate;
            index = i;
            break;
        }
    }
    if (!slot) {
        if (table->slots->length >= (int)RUNTIME_RESOURCE_INDEX_MASK) return 0;
        slot = (RuntimeResourceSlot*)mem_calloc(1, sizeof(RuntimeResourceSlot),
            MEM_CAT_SYSTEM);
        if (!slot || !arraylist_append(table->slots, slot)) {
            if (slot) mem_free(slot);
            return 0;
        }
        index = table->slots->length - 1;
    }
    slot->generation++;
    if (slot->generation == 0) slot->generation++;

    RuntimeResourceEntry* entry = (RuntimeResourceEntry*)mem_calloc(1,
        sizeof(RuntimeResourceEntry), MEM_CAT_SYSTEM);
    if (!entry) return 0;
    entry->id = ((uint32_t)slot->generation << RUNTIME_RESOURCE_INDEX_BITS) |
        (uint32_t)(index + 1);
    entry->root_slot = root_vector_count(&table->owner_values);
    entry->root_count = root_count;
    entry->lifecycle_owner = lifecycle_owner;
    entry->descriptor = descriptor;
    entry->close_callback = close_callback;
    entry->close_user = close_user;
    entry->is_handle = is_handle;
    for (int i = 0; i < root_count; i++) {
        Item rooted_value = (Item){.item = *roots.slot((size_t)i)};
        if (root_vector_push(&table->owner_values, rooted_value)) continue;
        root_vector_shrink(&table->owner_values, entry->root_slot);
        mem_free(entry);
        return 0;
    }
    slot->entry = entry;
    table->active_count++;
    return entry->id;
}

extern "C" uint32_t runtime_resource_table_add_root_span(
        RuntimeResourceTable* table, const Item* root_values, int root_count,
        const RuntimeResourceDescriptor* descriptor,
        RuntimeResourceCloseCallback close_callback, void* close_user,
        bool is_handle) {
    return runtime_resource_table_add_root_span_owned(table, NULL, root_values,
        root_count, descriptor, close_callback, close_user, is_handle);
}

extern "C" uint32_t runtime_resource_table_add(RuntimeResourceTable* table,
        Item value, const RuntimeResourceDescriptor* descriptor,
        RuntimeResourceCloseCallback close_callback, void* close_user,
        bool is_handle) {
    return runtime_resource_table_add_root_span_owned(table, NULL, &value, 1, descriptor,
        close_callback, close_user, is_handle);
}

extern "C" uint32_t runtime_resource_table_add_owned(RuntimeResourceTable* table,
        void* lifecycle_owner, Item value,
        const RuntimeResourceDescriptor* descriptor,
        RuntimeResourceCloseCallback close_callback, void* close_user,
        bool is_handle) {
    return runtime_resource_table_add_root_span_owned(table, lifecycle_owner,
        &value, 1, descriptor, close_callback, close_user, is_handle);
}

extern "C" void runtime_resource_table_close_group(RuntimeResourceTable* table,
        RuntimeResourceGroup group) {
    if (!table || group == RUNTIME_RESOURCE_GROUP_NONE || !table->slots) return;
    for (int i = 0; i < table->slots->length; i++) {
        RuntimeResourceSlot* slot = (RuntimeResourceSlot*)table->slots->data[i];
        RuntimeResourceEntry* entry = slot ? slot->entry : NULL;
        if (!entry || !entry->descriptor || entry->descriptor->group != group) continue;
        runtime_resource_table_release_entry(table, slot, true);
    }
}

extern "C" void runtime_resource_table_close_group_owned(
        RuntimeResourceTable* table, void* lifecycle_owner,
        RuntimeResourceGroup group) {
    if (!table || group == RUNTIME_RESOURCE_GROUP_NONE || !table->slots) return;
    for (int i = 0; i < table->slots->length; i++) {
        RuntimeResourceSlot* slot = (RuntimeResourceSlot*)table->slots->data[i];
        RuntimeResourceEntry* entry = slot ? slot->entry : NULL;
        if (!entry || entry->lifecycle_owner != lifecycle_owner ||
                !entry->descriptor || entry->descriptor->group != group) continue;
        runtime_resource_table_release_entry(table, slot, true);
    }
}
