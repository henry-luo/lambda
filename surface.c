#include "view.h"

#define STB_IMAGE_IMPLEMENTATION
#include "lib/stb_image.h"

typedef struct ImageEntry {
    // ImageFormat format;
    const char* path;  // todo: change to URL
    ImageSurface *image;
} ImageEntry;

int image_compare(const void *a, const void *b, void *udata) {
    const ImageEntry *fa = a;
    const ImageEntry *fb = b;
    return strcmp(fa->path, fb->path);
}

uint64_t image_hash(const void *item, uint64_t seed0, uint64_t seed1) {
    const ImageEntry *image = item;
    // xxhash3 is a fast hash function
    return hashmap_xxhash3(image->path, strlen(image->path), seed0, seed1);
}

ImageSurface* load_image(UiContext* uicon, const char *file_path) {
    if (uicon->image_cache == NULL) {
        // create a new hash map. 2nd argument is the initial capacity. 
        // 3rd and 4th arguments are optional seeds that are passed to the following hash function.
        uicon->image_cache = hashmap_new(sizeof(ImageEntry), 10, 0, 0, 
            image_hash, image_compare, NULL, NULL);
    }
    ImageEntry* entry = (ImageEntry*) hashmap_get(uicon->image_cache, &(ImageEntry){.path = file_path});
    if (entry) {
        printf("Image loaded from cache: %s\n", file_path);
        return entry->image;
    }
    else {
        printf("Image not found in cache: %s\n", file_path);
    }

    ImageSurface *surface;
    int slen = strlen(file_path);
    if (slen > 4 && strcmp(file_path + slen - 4, ".svg") == 0) {
        surface = (ImageSurface *)calloc(1, sizeof(ImageSurface));
        surface->format = IMAGE_FORMAT_SVG;
        surface->pic = tvg_picture_new();
        Tvg_Result ret = tvg_picture_load(surface->pic, file_path);
        if (ret != TVG_RESULT_SUCCESS) {
            printf("Failed to load SVG image: %s\n", file_path);
            tvg_paint_del(surface->pic);
            free(surface);
            return NULL;
        }
    }
    else {
        int width, height, channels;
        unsigned char *data = stbi_load(file_path, &width, &height, &channels, 4);
        if (!data) {
            printf("Failed to load image: %s\n", file_path);
            return NULL;
        }
        surface = image_surface_create_from(width, height, data);
        if (!surface) { stbi_image_free(data);  return NULL; }
        if (slen > 5 && strcmp(file_path + slen - 5, ".jpeg") == 0) {
            surface->format = IMAGE_FORMAT_JPEG;
        }
        else if (slen > 4 && strcmp(file_path + slen - 4, ".jpg") == 0) {
            surface->format = IMAGE_FORMAT_JPEG;
        }        
        else if (slen > 4 && strcmp(file_path + slen - 4, ".png") == 0) {
            surface->format = IMAGE_FORMAT_PNG;
        }
    }

    // copy the image path
    char* path = (char*)malloc(strlen(file_path) + 1);  strcpy(path, file_path);
    hashmap_set(uicon->image_cache, &(ImageEntry){.path = path, .image = surface});     
    return surface;
}

bool image_entry_free(const void *item, void *udata) {
    ImageEntry* entry = (ImageEntry*)item;
    free((char*)entry->path);
    image_surface_destroy(entry->image);
    return true;
}

void image_cache_cleanup(UiContext* uicon) {
    // loop through the hashmap and free the images
    if (uicon->image_cache) {
        printf("Cleaning up cached images\n");
        hashmap_scan(uicon->image_cache, image_entry_free, NULL);
        hashmap_free(uicon->image_cache);
        uicon->image_cache = NULL;
    }
}

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

void fill_surface_rect(ImageSurface* surface, Rect* rect, uint32_t color, Rect* clip) {
    Rect r;
    if (!surface) return;
    if (!rect) { r = (Rect){0, 0, surface->width, surface->height};  rect = &r; }
    printf("fill rect: x:%d, y:%d, wd:%d, hg:%d, color:%x\n", rect->x, rect->y, rect->width, rect->height, color);
    int left = max(clip->x, rect->x), right = min(clip->x + clip->width, rect->x + rect->width);
    int top = max(clip->y, rect->y), bottom = min(clip->x + clip->height, rect->y + rect->height);
    if (left >= right || top >= bottom) return; // rect outside the surface
    for (int i = top; i < bottom; i++) {
        uint8_t* row_pixels = (uint8_t*)surface->pixels + i * surface->pitch; // updated to use 'i'
        _fill_row(row_pixels, left, right - left, color);
    }
}

// a primitive blit function to copy pixels from src to dst
void blit_surface_scaled(ImageSurface* src, Rect* src_rect, ImageSurface* dst, Rect* dst_rect, Rect* clip) {
    Rect rect;
    if (!src || !dst || !dst_rect || !clip) return;
    if (!src_rect) { // use the entire source image
        rect = (Rect){0, 0, src->width, src->height};
        src_rect = &rect;
    }
    printf("blit surface: src(%d, %d, %d, %d) to dst(%d, %d, %d, %d)\n", 
        src_rect->x, src_rect->y, src_rect->width, src_rect->height, 
        dst_rect->x, dst_rect->y, dst_rect->width, dst_rect->height);
    float x_ratio = (float)src_rect->width / dst_rect->width;
    float y_ratio = (float)src_rect->height / dst_rect->height;
    int left = max(clip->x, dst_rect->x), right = min(clip->x + clip->width, dst_rect->x + dst_rect->width);
    int top = max(clip->y, dst_rect->y), bottom = min(clip->y + clip->height, dst_rect->y + dst_rect->height);
    if (left >= right || top >= bottom) return; // dst_rect outside the dst surface
    for (int i = top; i < bottom; i++) {
        uint8_t* row_pixels = (uint8_t*)dst->pixels + i * dst->pitch;
        for (int j = left; j < right; j++) {
            // todo: support different scale mode, like SDL_SCALEMODE_LINEAR
            int src_x = src_rect->x + (j - dst_rect->x) * x_ratio;
            int src_y = src_rect->y + (i - dst_rect->y) * y_ratio;
            uint8_t* src_pixel = (uint8_t*)src->pixels + (src_y * src->pitch) + (src_x * 4);
            uint8_t* dst_pixel = (uint8_t*)row_pixels + (j * 4);
            // hardcoded for ABGR to RGBA conversion
            dst_pixel[0] = src_pixel[0];  // dst alpha channel
            dst_pixel[1] = src_pixel[1];  // dst blue channel
            dst_pixel[2] = src_pixel[2];  // dst green channel
            dst_pixel[3] = src_pixel[3];  // dst red channel
        }
    }
}

void image_surface_destroy(ImageSurface* img_surface) {
    if (img_surface) {
        if (img_surface->pixels) free(img_surface->pixels);
        // if (img_surface->format == IMAGE_FORMAT_SVG && img_surface->pic) {
        //     tvg_paint_del(img_surface->pic);
        // }
        // free(img_surface);
    }
}