#include "view.hpp"

void image_surface_bump_generation(ImageSurface* img_surface) {
    if (!img_surface) return;
    img_surface->generation++;
    if (img_surface->generation == 0) img_surface->generation = 1;
}
