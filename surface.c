#include "view.h"

ImageSurface* image_surface_create(int pixel_width, int pixel_height) {
    if (pixel_width <= 0 || pixel_height <= 0) {
        fprintf(stderr, "Error: Invalid image surface dimensions.\n");
        return NULL;
    }
    ImageSurface* img_surface = calloc(1, sizeof(ImageSurface));
    img_surface->width = pixel_width;  img_surface->height = pixel_height;
    img_surface->pitch = pixel_width * 4;
    img_surface->pixels = calloc(pixel_width * pixel_height * 4, sizeof(uint32_t));
    if (!img_surface->pixels) {
        fprintf(stderr, "Error: Could not allocate memory for the image surface.\n");
        free(img_surface);
        return NULL;
    }
    return img_surface;
}

ImageSurface* image_surface_create_from(int pixel_width, int pixel_height, void* pixels) {
    if (pixel_width <= 0 || pixel_height <= 0 || !pixels) {
        fprintf(stderr, "Error: Invalid image surface dimensions or pixels.\n");
        return NULL;
    }
    ImageSurface* img_surface = calloc(1, sizeof(ImageSurface));
    if (img_surface) {
        img_surface->width = pixel_width;  img_surface->height = pixel_height;
        img_surface->pitch = pixel_width * 4;
        img_surface->pixels = pixels;
    }
    return img_surface;
}

void _fill_row(uint8_t* pixels, int x, int wd, uint32_t color) {
    uint32_t* pixel = (uint32_t*)pixels + x;  uint32_t* end = pixel + wd;
    while (pixel < end) { *pixel++ = color; }
}

void fill_surface_rect(ImageSurface* surface, Rect* rect, uint32_t color) {
    Rect r;
    if (!surface) return;
    if (!rect) { r = (Rect){0, 0, surface->width, surface->height};  rect = &r; }
    printf("fill rect: x:%d, y:%d, wd:%d, hg:%d, color:%x\n", rect->x, rect->y, rect->width, rect->height, color);
    for (int i = 0; i < rect->height; i++) {
        if (rect->y + i < 0 || rect->y + i >= surface->height) continue;
        uint8_t* row_pixels = (uint8_t*)surface->pixels + (rect->y + i) * surface->pitch;
        if (0 <= rect->x && rect->x + rect->width <= surface->width) {
            _fill_row(row_pixels, rect->x, rect->width, color);
        }
    }
}

// a primitive blit function to copy pixels from src to dst
void blit_surface_scaled(ImageSurface* src, Rect* src_rect, ImageSurface* dst, Rect* dst_rect) {
    Rect rect;
    if (!src || !dst || !dst_rect) return;
    if (!src_rect) {
        rect = (Rect){0, 0, src->width, src->height};
        src_rect = &rect;
    }
    printf("blit surface: src(%d, %d, %d, %d) to dst(%d, %d, %d, %d)\n", 
        src_rect->x, src_rect->y, src_rect->width, src_rect->height, dst_rect->x, dst_rect->y, dst_rect->width, dst_rect->height);
    float x_ratio = (float)src_rect->width / dst_rect->width;
    float y_ratio = (float)src_rect->height / dst_rect->height;
    for (int i = 0; i < dst_rect->height; i++) {
        if (dst_rect->y + i < 0 || dst_rect->y + i >= dst->height) continue;
        uint8_t* row_pixels = (uint8_t*)dst->pixels + (dst_rect->y + i) * dst->pitch;
        for (int j = 0; j < dst_rect->width; j++) {
            if (dst_rect->x + j < 0 || dst_rect->x + j >= dst->width) continue;
            // todo: support different scale mode, like SDL_SCALEMODE_LINEAR
            int src_x = src_rect->x + j * x_ratio;
            int src_y = src_rect->y + i * y_ratio;
            uint8_t* src_pixel = (uint8_t*)src->pixels + (src_y * src->pitch) + (src_x * 4);
            uint8_t* dst_pixel = (uint8_t*)row_pixels + (dst_rect->x + j) * 4;
            // hardcoded for ABGR to RGBA conversion
            dst_pixel[0] = src_pixel[3];  // dst alpha channel
            dst_pixel[1] = src_pixel[2];  // dst blue channel
            dst_pixel[2] = src_pixel[1];  // dst green channel
            dst_pixel[3] = src_pixel[0];  // dst red channel
        }
    }
}

void image_surface_destroy(ImageSurface* img_surface) {
    if (img_surface) {
        if (img_surface->pixels) free(img_surface->pixels);
        free(img_surface);
    }
}