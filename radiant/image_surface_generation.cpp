#include "view.hpp"
#include "../lib/slot_table.hpp"
#include "../lib/mem_factory.h"
#include <pthread.h>

// Process-wide slot table behind ImageSurface::self. Surfaces are created and
// destroyed on loader and render threads, and tile workers look handles up
// during replay, so every access takes the lock. The slot array lives in a
// process-lifetime pool.
static pthread_mutex_t g_image_slots_lock = PTHREAD_MUTEX_INITIALIZER;
static Pool* g_image_slots_pool = nullptr;
static lam::SlotTable<ImageSurface> g_image_slots;

ImageSurface* image_surface_alloc(void) {
    ImageSurface* surface = (ImageSurface*)mem_calloc(1, sizeof(ImageSurface), MEM_CAT_IMAGE);
    if (!surface) return nullptr;
    pthread_mutex_lock(&g_image_slots_lock);
    if (!g_image_slots_pool) {
        g_image_slots_pool = mem_pool_create(NULL, MEM_ROLE_MEDIA, "image_surface.slots");
        g_image_slots.init(g_image_slots_pool);
    }
    // a failed insert leaves a null handle: the surface works but is never retained
    surface->self = g_image_slots.insert(surface);
    pthread_mutex_unlock(&g_image_slots_lock);
    return surface;
}

ImageSurface* image_surface_lookup(lam::Handle<ImageSurface> handle) {
    if (handle.is_null()) return nullptr;
    pthread_mutex_lock(&g_image_slots_lock);
    ImageSurface* surface = g_image_slots.lookup(handle);
    pthread_mutex_unlock(&g_image_slots_lock);
    return surface;
}

void image_surface_release_slot(ImageSurface* surface) {
    if (!surface || surface->self.is_null()) return;
    pthread_mutex_lock(&g_image_slots_lock);
    g_image_slots.release(surface->self);
    pthread_mutex_unlock(&g_image_slots_lock);
    surface->self = lam::Handle<ImageSurface>{};
}

void image_surface_bump_generation(ImageSurface* img_surface) {
    if (!img_surface) return;
    img_surface->generation++;
    if (img_surface->generation == 0) img_surface->generation = 1;
}

void image_surface_detach_pixels(ImageSurface* img_surface) {
    if (!img_surface) return;
    // Borrowed buffers can be freed independently; detachment must invalidate every cached paint.
    img_surface->pixels = nullptr;
    image_surface_bump_generation(img_surface);
}
