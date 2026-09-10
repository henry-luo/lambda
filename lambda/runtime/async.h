#pragma once

// Runtime-owned async storage primitives. This header deliberately depends
// only on Lambda's Item/Array representation; JS and Lambda task policy stay
// in their respective frontends (D6.3.1, JR7.1).
#include "../lambda.hpp"
#include "root_vector.h"
#include "../../lib/arraylist.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum RuntimeJobKind {
    RUNTIME_JOB_NONE = 0,
    RUNTIME_JOB_NEXT_TICK,
    RUNTIME_JOB_MICROTASK,
    RUNTIME_JOB_UNHANDLED_REJECTION,
    RUNTIME_JOB_ANIMATION_FRAME,
    RUNTIME_JOB_TIMER,
} RuntimeJobKind;

// One async-context capture travels with every queued JavaScript callback.
// Queue policy remains external: nextTick, Promise jobs and RAF drain at
// different checkpoints even though they share this carrier (D6.3.1).
typedef struct RuntimeAsyncContextSnapshot {
    Item resource;
    Item als_context;
    Item domain;
} RuntimeAsyncContextSnapshot;

// The queue serializes this one job shape into its GC-owned Item storage.
// `arguments` is an optional Array Item; `id` is meaningful only to kinds
// with externally cancellable work such as animation frames and timers.
typedef struct RuntimeJob {
    Item callback;
    Item arguments;
    RuntimeAsyncContextSnapshot context;
    int64_t id;
    RuntimeJobKind kind;
} RuntimeJob;

typedef struct RuntimeJobQueue {
    Item* storage_owner;
    int64_t head;
    int64_t pending_count;
} RuntimeJobQueue;

// One owner can retain callbacks for native operations that complete out of
// order. Requests carry only a stable slot index; releasing a callback never
// moves a live slot (D5.1.1v2; JSCU31).
typedef struct RuntimeCallbackSlots {
    RootVector values;
} RuntimeCallbackSlots;

// One native operation can retain a fixed set of durable JS values for its
// whole lifecycle. Slots begin as the all-zero absent Item so migrated native
// fields preserve their existing presence tests. Callback queues use
// RuntimeCallbackSlots when completion order is part of their protocol
// (D5.1.1v2; JSCU31).
typedef struct RuntimeValueSlots {
    RootVector values;
} RuntimeValueSlots;

// A context-owned native resource has one generation-checked identity and one
// rooted script owner. Individual protocols retain only their native payload
// behind this common lifecycle record (D7.4.1v2; JSCU31).
typedef enum RuntimeResourceKind {
    RUNTIME_RESOURCE_NONE = 0,
    RUNTIME_RESOURCE_TIMER,
    RUNTIME_RESOURCE_TCP_SOCKET,
    RUNTIME_RESOURCE_TCP_SERVER,
    RUNTIME_RESOURCE_CRYPTO_HMAC,
    RUNTIME_RESOURCE_CRYPTO_HASH,
    RUNTIME_RESOURCE_CRYPTO_SIGN,
    RUNTIME_RESOURCE_CRYPTO_CIPHER,
    RUNTIME_RESOURCE_FS_REQUEST,
    RUNTIME_RESOURCE_DNS_REQUEST,
    RUNTIME_RESOURCE_TLS_SOCKET,
} RuntimeResourceKind;

typedef enum RuntimeResourceGroup {
    RUNTIME_RESOURCE_GROUP_NONE = 0,
    RUNTIME_RESOURCE_GROUP_NETWORK,
    RUNTIME_RESOURCE_GROUP_CRYPTO,
} RuntimeResourceGroup;

typedef struct RuntimeResourceDescriptor {
    RuntimeResourceKind kind;
    RuntimeResourceGroup group;
    const char* display_name;
} RuntimeResourceDescriptor;

typedef void (*RuntimeResourceCloseCallback)(void* user);

typedef struct RuntimeResourceEntry {
    uint32_t id;
    int64_t root_slot;
    int root_count;
    // Partitions one context-wide table by lifecycle without introducing a
    // second resource registry.  It is an opaque stable native owner.
    void* lifecycle_owner;
    const RuntimeResourceDescriptor* descriptor;
    RuntimeResourceCloseCallback close_callback;
    void* close_user;
    bool is_handle;
    bool closing;
} RuntimeResourceEntry;

typedef struct RuntimeResourceSlot {
    uint16_t generation;
    RuntimeResourceEntry* entry;
} RuntimeResourceSlot;

typedef struct RuntimeResourceTable {
    RootVector owner_values;
    ArrayList* slots;
    int active_count;
} RuntimeResourceTable;

void runtime_job_queue_init(RuntimeJobQueue* queue, Item* storage_owner);
bool runtime_job_queue_push(RuntimeJobQueue* queue, const RuntimeJob* job);
bool runtime_job_queue_pop(RuntimeJobQueue* queue, RuntimeJob* job);
int64_t runtime_job_queue_size(const RuntimeJobQueue* queue);
bool runtime_job_queue_cancel(RuntimeJobQueue* queue, int64_t id);
void runtime_job_queue_clear(RuntimeJobQueue* queue);

void runtime_callback_slots_init(RuntimeCallbackSlots* slots, Context* owner,
                                 const char* name);
bool runtime_callback_slots_add(RuntimeCallbackSlots* slots, Item callback,
                                int64_t* out_slot);
Item runtime_callback_slots_take(RuntimeCallbackSlots* slots, int64_t slot);
void runtime_callback_slots_destroy(RuntimeCallbackSlots* slots);

bool runtime_value_slots_init(RuntimeValueSlots* slots, Context* owner,
                              const char* name, int count);
Item runtime_value_slots_get(RuntimeValueSlots* slots, int slot);
void runtime_value_slots_set(RuntimeValueSlots* slots, int slot, Item value);
void runtime_value_slots_destroy(RuntimeValueSlots* slots);

const RuntimeResourceDescriptor* runtime_resource_descriptor_from_legacy_name(
    const char* name);
void runtime_resource_table_init(RuntimeResourceTable* table, Context* owner,
                                 const char* name);
void runtime_resource_table_clear(RuntimeResourceTable* table);
void runtime_resource_table_destroy(RuntimeResourceTable* table);
uint32_t runtime_resource_table_add(RuntimeResourceTable* table, Item value,
    const RuntimeResourceDescriptor* descriptor,
    RuntimeResourceCloseCallback close_callback, void* close_user,
    bool is_handle);
uint32_t runtime_resource_table_add_owned(RuntimeResourceTable* table,
    void* lifecycle_owner, Item value,
    const RuntimeResourceDescriptor* descriptor,
    RuntimeResourceCloseCallback close_callback, void* close_user,
    bool is_handle);
// A resource row may own a semantic group of exact roots. The first Item is
// the script-visible resource owner used for identity lookup.
uint32_t runtime_resource_table_add_root_span(RuntimeResourceTable* table,
    const Item* root_values, int root_count,
    const RuntimeResourceDescriptor* descriptor,
    RuntimeResourceCloseCallback close_callback, void* close_user,
    bool is_handle);
uint32_t runtime_resource_table_add_root_span_owned(RuntimeResourceTable* table,
    void* lifecycle_owner, const Item* root_values, int root_count,
    const RuntimeResourceDescriptor* descriptor,
    RuntimeResourceCloseCallback close_callback, void* close_user,
    bool is_handle);
void runtime_resource_table_remove(RuntimeResourceTable* table, uint32_t id);
void runtime_resource_table_remove_owned(RuntimeResourceTable* table,
    void* lifecycle_owner, uint32_t id);
// Unsafe-recovery paths detach an already-invalid native payload without
// invoking its close operation; ordinary owners must use remove instead.
void runtime_resource_table_forget(RuntimeResourceTable* table, uint32_t id);
void runtime_resource_table_forget_owned(RuntimeResourceTable* table,
    void* lifecycle_owner, uint32_t id);
void runtime_resource_table_clear_owned(RuntimeResourceTable* table,
    void* lifecycle_owner);
const RuntimeResourceEntry* runtime_resource_table_entry(
    RuntimeResourceTable* table, uint32_t id);
const RuntimeResourceEntry* runtime_resource_table_entry_owned(
    RuntimeResourceTable* table, void* lifecycle_owner, uint32_t id);
const RuntimeResourceEntry* runtime_resource_table_entry_at(
    RuntimeResourceTable* table, int index);
int runtime_resource_table_slot_count(const RuntimeResourceTable* table);
int runtime_resource_table_active_count(const RuntimeResourceTable* table);
int runtime_resource_table_active_count_owned(const RuntimeResourceTable* table,
    void* lifecycle_owner);
Item runtime_resource_table_value(RuntimeResourceTable* table,
                                  const RuntimeResourceEntry* entry);
void* runtime_resource_table_user_data(RuntimeResourceTable* table,
    uint32_t id);
void* runtime_resource_table_user_data_owned(RuntimeResourceTable* table,
    void* lifecycle_owner, uint32_t id);
void runtime_resource_table_close_group(RuntimeResourceTable* table,
    RuntimeResourceGroup group);
void runtime_resource_table_close_group_owned(RuntimeResourceTable* table,
    void* lifecycle_owner, RuntimeResourceGroup group);

#ifdef __cplusplus
}
#endif
