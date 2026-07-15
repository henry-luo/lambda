#include "view.hpp"

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
