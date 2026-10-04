#include "view.hpp"
#include "render.hpp"
#include "../lib/mem_factory.h"
#include "../lib/ownership.hpp"
#include <pthread.h>
#include "../lib/generation.h"

// Process-wide surface table behind ImageSurface::self (design: lock-free
// lookup with quiet-point retirement).
//
// Readers (display-list replay, tile workers, retained checks, export
// lowering) take no lock: a handle indexes a slot holding the surface pointer
// and a generation. Slots live in fixed-size chunks that are never moved or
// freed, so an index always maps to the same address.
//
// A destroyed surface is not freed while any ImageSurfaceReadScope is open: its
// slot pointer is nulled at once (new readers see it gone) and the surface is
// queued. The scope that closes last on a non-worker thread releases the queue;
// only then is the slot returned to the free list with its generation advanced.
// A pointer read inside a scope therefore stays valid until the scope ends.
//
// Writers (alloc, destroy, release) serialize on one lock; readers never take it.

namespace {

struct ImageSlot {
    ImageSurface* target;  // atomic: null once the surface is destroyed
    uint32_t gen;          // atomic: advanced when the slot is released for reuse
    uint32_t next_free;    // 1-based free-list link, writer lock only
};

constexpr uint32_t IMAGE_SLOT_CHUNK_BITS = 10;
constexpr uint32_t IMAGE_SLOT_CHUNK_SIZE = 1u << IMAGE_SLOT_CHUNK_BITS;
constexpr uint32_t IMAGE_SLOT_MAX_CHUNKS = 4096;  // 4M surfaces

}  // namespace

static pthread_mutex_t g_image_slots_lock = PTHREAD_MUTEX_INITIALIZER;
static Pool* g_image_slots_pool = nullptr;             // process-lifetime chunk storage
static ImageSlot* g_image_slot_chunks[IMAGE_SLOT_MAX_CHUNKS];  // atomic entries, published once
static uint32_t g_image_slots_used = 0;                // slots ever handed out (writer lock)
static uint32_t g_image_slots_free_head = 0;           // 1-based (writer lock)
static int32_t g_image_read_scopes = 0;                // atomic: open read scopes
static lam::Own<ImageSurface> g_image_retire_head;     // writer lock
static size_t g_image_retire_count = 0;                // atomic; changed under the writer lock
static thread_local bool tl_image_render_worker = false;

static ImageSlot* image_slot_at(uint32_t index) {
    ImageSlot* chunk = __atomic_load_n(&g_image_slot_chunks[index >> IMAGE_SLOT_CHUNK_BITS], __ATOMIC_ACQUIRE);
    return chunk ? &chunk[index & (IMAGE_SLOT_CHUNK_SIZE - 1)] : nullptr;
}

// writer lock held; returns a slot index or UINT32_MAX
static uint32_t image_slot_acquire_locked() {
    if (g_image_slots_free_head) {
        uint32_t index = g_image_slots_free_head - 1;
        g_image_slots_free_head = image_slot_at(index)->next_free;
        return index;
    }
    uint32_t index = g_image_slots_used;
    uint32_t chunk = index >> IMAGE_SLOT_CHUNK_BITS;
    if (chunk >= IMAGE_SLOT_MAX_CHUNKS) return UINT32_MAX;
    if (!g_image_slot_chunks[chunk]) {
        if (!g_image_slots_pool) {
            g_image_slots_pool = mem_pool_create(mem_context_process(MEM_ROLE_MEDIA), MEM_ROLE_MEDIA, "image_surface.slots");
            if (!g_image_slots_pool) return UINT32_MAX;
        }
        ImageSlot* storage = (ImageSlot*)pool_calloc(g_image_slots_pool, sizeof(ImageSlot) * IMAGE_SLOT_CHUNK_SIZE);
        if (!storage) return UINT32_MAX;
        // readers index the chunk only after this store publishes it
        __atomic_store_n(&g_image_slot_chunks[chunk], storage, __ATOMIC_RELEASE);
    }
    g_image_slots_used++;
    return index;
}

// writer lock held: the slot's handles go stale and it can be reused
static void image_slot_release_locked(lam::Handle<ImageSurface> handle) {
    ImageSlot* slot = image_slot_at(handle.index);
    if (!slot || __atomic_load_n(&slot->gen, __ATOMIC_ACQUIRE) != handle.gen) return;
    __atomic_store_n(&slot->target, (ImageSurface*)nullptr, __ATOMIC_SEQ_CST);
    uint32_t next = generation_next32(handle.gen);
    __atomic_store_n(&slot->gen, next, __ATOMIC_RELEASE);
    // a slot whose generation would wrap is retired for good
    if (next == 1) return;
    slot->next_free = g_image_slots_free_head;
    g_image_slots_free_head = handle.index + 1;
}

ImageSurface* image_surface_alloc(void) {
    ImageSurface* surface = (ImageSurface*)mem_calloc(1, sizeof(ImageSurface), MEM_CAT_IMAGE); // OBJ_HEAP_OK: the one heap ImageSurface allocator; the owner releases it with image_surface_destroy
    if (!surface) return nullptr;
    pthread_mutex_lock(&g_image_slots_lock);
    uint32_t index = image_slot_acquire_locked();
    // a failed acquire leaves a null handle: the surface works but is never retained
    if (index != UINT32_MAX) {
        ImageSlot* slot = image_slot_at(index);
        uint32_t gen = generation_next32(__atomic_load_n(&slot->gen, __ATOMIC_ACQUIRE));
        __atomic_store_n(&slot->gen, gen, __ATOMIC_RELEASE);
        __atomic_store_n(&slot->target, surface, __ATOMIC_RELEASE);
        surface->self = lam::Handle<ImageSurface>{index, gen};
    }
    pthread_mutex_unlock(&g_image_slots_lock);
    return surface;
}

ImageSurface* image_surface_lookup(lam::Handle<ImageSurface> handle) {
    if (handle.is_null() || handle.index >= IMAGE_SLOT_MAX_CHUNKS * IMAGE_SLOT_CHUNK_SIZE) return nullptr;
    ImageSlot* slot = image_slot_at(handle.index);
    if (!slot) return nullptr;
    // the generation read on both sides of the pointer rules out a slot that
    // was released and reused while it was being read
    uint32_t before = __atomic_load_n(&slot->gen, __ATOMIC_ACQUIRE);
    ImageSurface* surface = __atomic_load_n(&slot->target, __ATOMIC_SEQ_CST);
    uint32_t after = __atomic_load_n(&slot->gen, __ATOMIC_ACQUIRE);
    return before == handle.gen && after == handle.gen ? surface : nullptr;
}

void image_surface_release_slot(ImageSurface* surface) {
    if (!surface || surface->self.is_null()) return;
    pthread_mutex_lock(&g_image_slots_lock);
    image_slot_release_locked(surface->self);
    pthread_mutex_unlock(&g_image_slots_lock);
    surface->self = lam::Handle<ImageSurface>{};
}

void image_surface_release_now(ImageSurface* surface) {
    if (!surface) return;
    // the release hands the surface over; borrowed pixels stay with their owner
    lam::Temp<ImageSurface> owned(surface);
    image_surface_release_slot(surface);
    lam::free_owned(surface->owned_pixels);
    surface->pixels = nullptr;
    if (surface->pic) rdt_picture_free(surface->pic);
    lam::free_owned(surface->source_path);
    lam::free_owned(surface->source_data);
}

bool image_surface_defer_release(ImageSurface* surface) {
    if (!surface) return false;
    pthread_mutex_lock(&g_image_slots_lock);
    // new readers stop seeing the surface before the open-scope check (both
    // sequentially consistent: a reader that opened a scope first is counted,
    // a later one reads the null)
    if (!surface->self.is_null()) {
        ImageSlot* slot = image_slot_at(surface->self.index);
        if (slot && __atomic_load_n(&slot->gen, __ATOMIC_ACQUIRE) == surface->self.gen)
            __atomic_store_n(&slot->target, (ImageSurface*)nullptr, __ATOMIC_SEQ_CST);
    }
    bool deferred = __atomic_load_n(&g_image_read_scopes, __ATOMIC_SEQ_CST) > 0;
    if (deferred) {
        surface->retire_next = g_image_retire_head;
        g_image_retire_head = lam::own(surface);
        __atomic_add_fetch(&g_image_retire_count, 1, __ATOMIC_RELEASE);
    }
    pthread_mutex_unlock(&g_image_slots_lock);
    return deferred;
}

void image_surface_drain_retired(void) {
    pthread_mutex_lock(&g_image_slots_lock);
    if (__atomic_load_n(&g_image_read_scopes, __ATOMIC_SEQ_CST) > 0) {
        pthread_mutex_unlock(&g_image_slots_lock);
        return;
    }
    ImageSurface* queue = g_image_retire_head;
    size_t count = __atomic_exchange_n(&g_image_retire_count, (size_t)0, __ATOMIC_ACQ_REL);
    g_image_retire_head = nullptr;
    pthread_mutex_unlock(&g_image_slots_lock);
    if (count > 64) log_debug("image_surface: releasing %zu retired surfaces", count);
    while (queue) {
        ImageSurface* next = queue->retire_next;
        queue->retire_next = nullptr;
        image_surface_release_now(queue);
        queue = next;
    }
}

void image_surface_mark_render_worker_thread(void) {
    tl_image_render_worker = true;
}

ImageSurfaceReadScope::ImageSurfaceReadScope() {
    __atomic_add_fetch(&g_image_read_scopes, 1, __ATOMIC_SEQ_CST);
}

ImageSurfaceReadScope::~ImageSurfaceReadScope() {
    int32_t open = __atomic_sub_fetch(&g_image_read_scopes, 1, __ATOMIC_SEQ_CST);
    // render workers free nothing (pictures and pixel buffers are released on
    // the host thread); their dispatcher's scope closes after them
    if (open == 0 && !tl_image_render_worker &&
        __atomic_load_n(&g_image_retire_count, __ATOMIC_ACQUIRE) > 0) image_surface_drain_retired();
}

void image_surface_bump_generation(ImageSurface* img_surface) {
    if (!img_surface) return;
    img_surface->generation = generation_next(img_surface->generation);
}

void image_surface_detach_pixels(ImageSurface* img_surface) {
    if (!img_surface) return;
    // Borrowed buffers can be freed independently; detachment must invalidate every cached paint.
    img_surface->pixels = nullptr;
    image_surface_bump_generation(img_surface);
}
