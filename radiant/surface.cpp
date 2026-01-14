#include "view.hpp"

#include "../lib/image.h"
#include "../lib/log.h"
#include "../lib/memtrack.h"
#include "../lambda/input/input.hpp"  // for download_http_content
#include <algorithm>  // for std::max, std::min
typedef struct ImageEntry {
    // ImageFormat format;
    const char* path;  // todo: change to URL
    ImageSurface *image;
} ImageEntry;

int image_compare(const void *a, const void *b, void *udata) {
    const ImageEntry *fa = (const ImageEntry*)a;
    const ImageEntry *fb = (const ImageEntry*)b;
    return strcmp(fa->path, fb->path);
}

uint64_t image_hash(const void *item, uint64_t seed0, uint64_t seed1) {
    const ImageEntry *image = (const ImageEntry*)item;
    // xxhash3 is a fast hash function
    return hashmap_xxhash3(image->path, strlen(image->path), seed0, seed1);
}

// Detect if memory content is SVG by checking for XML/SVG signature
static bool is_svg_content(const unsigned char* data, size_t size) {
    if (!data || size < 10) return false;
    
    // Skip UTF-8 BOM if present
    size_t offset = 0;
    if (size >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF) {
        offset = 3;
    }
    
    // Skip whitespace
    while (offset < size && (data[offset] == ' ' || data[offset] == '\t' || 
                              data[offset] == '\n' || data[offset] == '\r')) {
        offset++;
    }
    
    // Check for XML declaration or SVG tag
    if (size - offset >= 5) {
        if (strncmp((const char*)data + offset, "<?xml", 5) == 0 ||
            strncmp((const char*)data + offset, "<svg", 4) == 0) {
            return true;
        }
    }
    return false;
}

ImageSurface* load_image(UiContext* uicon, const char *img_url) {
    if (uicon->document == NULL || uicon->document->url == NULL) {
        log_error("Missing URL context for image: %s", img_url);
        return NULL;
    }
    Url* abs_url = parse_url(uicon->document->url, img_url);
    if (!abs_url) {
        log_error("Failed to parse URL: %s", img_url);
        return NULL;
    }
    
    // Check if this is an HTTP URL
    bool is_http = (abs_url->scheme == URL_SCHEME_HTTP || abs_url->scheme == URL_SCHEME_HTTPS);
    char* file_path = nullptr;
    char* temp_file_path = nullptr;
    unsigned char* downloaded_data = nullptr;
    size_t downloaded_size = 0;
    
    if (is_http) {
        // Download the image from HTTP URL
        const char* url_str = url_get_href(abs_url);
        log_debug("[image] Downloading image from URL: %s", url_str);
        downloaded_data = (unsigned char*)download_http_content(url_str, &downloaded_size, nullptr);
        if (!downloaded_data || downloaded_size == 0) {
            log_error("[image] Failed to download image: %s", url_str);
            url_destroy(abs_url);
            return NULL;
        }
        log_debug("[image] Downloaded image: %zu bytes", downloaded_size);
        // Use URL as cache key
        file_path = (char*)url_str;
    } else {
        file_path = url_to_local_path(abs_url);
        if (!file_path) {
            log_error("Invalid local URL: %s", img_url);
            url_destroy(abs_url);
            return NULL;
        }
    }

    if (uicon->image_cache == NULL) {
        // create a new hash map. 2nd argument is the initial capacity.
        // 3rd and 4th arguments are optional seeds that are passed to the following hash function.
        uicon->image_cache = hashmap_new(sizeof(ImageEntry), 10, 0, 0,
            image_hash, image_compare, NULL, NULL);
    }
    ImageEntry search_key = {.path = (char*)file_path, .image = NULL};
    ImageEntry* entry = (ImageEntry*) hashmap_get(uicon->image_cache, &search_key);
    if (entry) {
        log_debug("Image loaded from cache: %s", file_path);
        url_destroy(abs_url);
        return entry->image;
    }
    else {
        log_debug("Image not found in cache: %s", file_path);
    }

    ImageSurface *surface;
    int slen = strlen(file_path);
    // load image data
    log_debug("loading image at: %s", file_path);
    
    // Determine if this is an SVG - check content for HTTP, extension for local files
    bool is_svg = false;
    if (is_http && downloaded_data) {
        is_svg = is_svg_content(downloaded_data, downloaded_size);
        log_debug("[image] HTTP image format detection: is_svg=%s", is_svg ? "yes" : "no");
    } else {
        is_svg = (slen > 4 && strcmp(file_path + slen - 4, ".svg") == 0);
    }
    
    if (is_svg) {
        surface = (ImageSurface *)mem_calloc(1, sizeof(ImageSurface), MEM_CAT_IMAGE);
        surface->format = IMAGE_FORMAT_SVG;
        surface->pic = tvg_picture_new();
        Tvg_Result ret;
        if (is_http && downloaded_data) {
            ret = tvg_picture_load_data(surface->pic, (const char*)downloaded_data, (uint32_t)downloaded_size, "svg", NULL, false);
        } else {
            ret = tvg_picture_load(surface->pic, file_path);
        }
        if (ret != TVG_RESULT_SUCCESS) {
            log_debug("failed to load SVG image: %s", file_path);
            tvg_paint_unref(surface->pic, true);
            mem_free(surface);
            if (downloaded_data) free(downloaded_data);
            return NULL;
        }
        float svg_w, svg_h;
        tvg_picture_get_size(surface->pic, &svg_w, &svg_h);
        surface->width = svg_w;
        surface->height = svg_h;
        log_debug("SVG image size: %f x %f\n", svg_w, svg_h);
        if (downloaded_data) free(downloaded_data);
    }
    else {
        int width, height, channels;
        unsigned char *data;
        if (is_http && downloaded_data) {
            data = image_load_from_memory(downloaded_data, downloaded_size, &width, &height, &channels);
            free(downloaded_data);
        } else {
            data = image_load(file_path, &width, &height, &channels, 4);
        }
        if (!data) {
            log_debug("failed to load image: %s", file_path);
            return NULL;
        }
        surface = image_surface_create_from(width, height, data);
        if (!surface) { image_free(data);  return NULL; }
        if (slen > 5 && strcmp(file_path + slen - 5, ".jpeg") == 0) {
            surface->format = IMAGE_FORMAT_JPEG;
        }
        else if (slen > 4 && strcmp(file_path + slen - 4, ".jpg") == 0) {
            surface->format = IMAGE_FORMAT_JPEG;
        }
        else if (slen > 4 && strcmp(file_path + slen - 4, ".png") == 0) {
            surface->format = IMAGE_FORMAT_PNG;
        }
        else if (slen > 4 && strcmp(file_path + slen - 4, ".gif") == 0) {
            surface->format = IMAGE_FORMAT_GIF;
        }
    }
    surface->url = abs_url;

    ImageEntry new_entry = {.path = (char*)file_path, .image = surface};
    hashmap_set(uicon->image_cache, &new_entry);
    return surface;
}

// ============================================================================
// ThorVG Picture Integration
// ============================================================================

/**
 * Create a ThorVG Picture from an ImageSurface
 * 
 * This provides unified image loading for ThorVG integration - images are
 * loaded once via Radiant's load_image() and can then be used with ThorVG
 * rendering without needing ThorVG's image loaders.
 * 
 * @param surface The ImageSurface containing RGBA pixel data
 * @return ThorVG Paint object (Picture) or nullptr on failure
 *         Caller is responsible for managing the ThorVG object lifecycle
 */
Tvg_Paint create_tvg_picture_from_surface(ImageSurface* surface) {
    if (!surface || !surface->pixels) {
        log_debug("create_tvg_picture_from_surface: invalid surface");
        return nullptr;
    }
    
    // skip SVG surfaces - they already have a ThorVG picture
    if (surface->format == IMAGE_FORMAT_SVG && surface->pic) {
        log_debug("create_tvg_picture_from_surface: surface is SVG, returning existing pic");
        return surface->pic;
    }
    
    Tvg_Paint pic = tvg_picture_new();
    if (!pic) {
        log_debug("create_tvg_picture_from_surface: failed to create picture");
        return nullptr;
    }
    
    // Load raw RGBA pixels into ThorVG Picture
    // Note: TVG_COLORSPACE_ARGB8888 matches Radiant's pixel format (BGRA with alpha in high byte)
    Tvg_Result result = tvg_picture_load_raw(
        pic,
        (uint32_t*)surface->pixels,
        surface->width,
        surface->height,
        TVG_COLORSPACE_ABGR8888,  // Match Radiant's ABGR format (alpha, blue, green, red)
        false  // Don't copy - surface manages memory, caller must ensure surface outlives picture
    );
    
    if (result != TVG_RESULT_SUCCESS) {
        log_debug("create_tvg_picture_from_surface: tvg_picture_load_raw failed (%d)", result);
        tvg_paint_unref(pic, true);
        return nullptr;
    }
    
    log_debug("create_tvg_picture_from_surface: created %dx%d picture", surface->width, surface->height);
    return pic;
}

bool image_entry_free(const void *item, void *udata) {
    ImageEntry* entry = (ImageEntry*)item;
    free((char*)entry->path);  // path is from url_to_local_path() which uses stdlib malloc
    if (entry->image->url) url_destroy(entry->image->url);
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
    ImageSurface* img_surface = (ImageSurface*)mem_calloc(1, sizeof(ImageSurface), MEM_CAT_IMAGE);
    img_surface->width = pixel_width;  img_surface->height = pixel_height;
    img_surface->pitch = pixel_width * 4;
    img_surface->pixels = mem_calloc(pixel_width * pixel_height * 4, sizeof(uint32_t), MEM_CAT_IMAGE);
    if (!img_surface->pixels) {
        fprintf(stderr, "Error: Could not allocate memory for the image surface.\n");
        mem_free(img_surface);
        return NULL;
    }
    return img_surface;
}

ImageSurface* image_surface_create_from(int pixel_width, int pixel_height, void* pixels) {
    if (pixel_width <= 0 || pixel_height <= 0 || !pixels) {
        fprintf(stderr, "Error: Invalid image surface dimensions or pixels.\n");
        return NULL;
    }
    ImageSurface* img_surface = (ImageSurface*)mem_calloc(1, sizeof(ImageSurface), MEM_CAT_IMAGE);
    if (img_surface) {
        img_surface->width = pixel_width;  img_surface->height = pixel_height;
        img_surface->pitch = pixel_width * 4;
        img_surface->pixels = pixels;
    }
    return img_surface;
}

void _fill_row(uint8_t* pixels, int x, int wd, uint32_t color) {
    uint32_t* pixel = (uint32_t*)pixels + x;
    uint32_t* end = pixel + wd;

    // Extract source alpha from color (ABGR format: alpha in high byte)
    uint8_t src_a = (color >> 24) & 0xFF;

    if (src_a == 255) {
        // Fully opaque - fast path, just copy
        while (pixel < end) { *pixel++ = color; }
    } else if (src_a > 0) {
        // Semi-transparent - alpha blend with existing pixels
        uint8_t src_r = color & 0xFF;
        uint8_t src_g = (color >> 8) & 0xFF;
        uint8_t src_b = (color >> 16) & 0xFF;
        uint8_t inv_a = 255 - src_a;

        while (pixel < end) {
            uint32_t dst = *pixel;
            uint8_t dst_r = dst & 0xFF;
            uint8_t dst_g = (dst >> 8) & 0xFF;
            uint8_t dst_b = (dst >> 16) & 0xFF;

            // Alpha blend: result = src * src_a + dst * (1 - src_a)
            uint8_t out_r = (src_r * src_a + dst_r * inv_a) / 255;
            uint8_t out_g = (src_g * src_a + dst_g * inv_a) / 255;
            uint8_t out_b = (src_b * src_a + dst_b * inv_a) / 255;

            *pixel++ = (255 << 24) | (out_b << 16) | (out_g << 8) | out_r;
        }
    }
    // else src_a == 0: fully transparent, don't draw anything
}

void fill_surface_rect(ImageSurface* surface, Rect* rect, uint32_t color, Bound* clip) {
    Rect r;
    if (!surface || !surface->pixels) return;
    if (!rect) { r = (Rect){0, 0, (float)surface->width, (float)surface->height};  rect = &r; }
    log_debug("fill rect: x:%.0f, y:%.0f, wd:%.0f, hg:%.0f, color:%x", rect->x, rect->y, rect->width, rect->height, color);

    // Use explicit std::max/min to avoid template resolution issues
    int left = (int)std::max(clip->left, rect->x);
    int right = (int)std::min(clip->right, rect->x + rect->width);
    int top = (int)std::max(clip->top, rect->y);
    int bottom = (int)std::min(clip->bottom, rect->y + rect->height);
    if (left >= right || top >= bottom) return; // rect outside clip
    for (int i = top; i < bottom; i++) {
        uint8_t* row_pixels = (uint8_t*)surface->pixels + i * surface->pitch;
        _fill_row(row_pixels, left, right - left, color);
    }
}

// Bilinear interpolation helper function
static uint32_t bilinear_interpolate(ImageSurface* src, float src_x, float src_y) {
    int x1 = (int)src_x;
    int y1 = (int)src_y;
    int x2 = x1 + 1;
    int y2 = y1 + 1;

    // clamp coordinates to source bounds
    x1 = std::max(0, std::min(x1, src->width - 1));
    y1 = std::max(0, std::min(y1, src->height - 1));
    x2 = std::max(0, std::min(x2, src->width - 1));
    y2 = std::max(0, std::min(y2, src->height - 1));

    float fx = src_x - (int)src_x;
    float fy = src_y - (int)src_y;

    // get the four surrounding pixels
    uint32_t* p11 = (uint32_t*)((uint8_t*)src->pixels + y1 * src->pitch + x1 * 4);
    uint32_t* p21 = (uint32_t*)((uint8_t*)src->pixels + y1 * src->pitch + x2 * 4);
    uint32_t* p12 = (uint32_t*)((uint8_t*)src->pixels + y2 * src->pitch + x1 * 4);
    uint32_t* p22 = (uint32_t*)((uint8_t*)src->pixels + y2 * src->pitch + x2 * 4);

    // extract RGBA components for each pixel (little-endian: RGBA bytes -> ABGR uint32)
    uint8_t r11 = *p11 & 0xFF, g11 = (*p11 >> 8) & 0xFF, b11 = (*p11 >> 16) & 0xFF, a11 = (*p11 >> 24) & 0xFF;
    uint8_t r21 = *p21 & 0xFF, g21 = (*p21 >> 8) & 0xFF, b21 = (*p21 >> 16) & 0xFF, a21 = (*p21 >> 24) & 0xFF;
    uint8_t r12 = *p12 & 0xFF, g12 = (*p12 >> 8) & 0xFF, b12 = (*p12 >> 16) & 0xFF, a12 = (*p12 >> 24) & 0xFF;
    uint8_t r22 = *p22 & 0xFF, g22 = (*p22 >> 8) & 0xFF, b22 = (*p22 >> 16) & 0xFF, a22 = (*p22 >> 24) & 0xFF;

    // bilinear interpolation for each component
    uint8_t r = (uint8_t)(r11 * (1 - fx) * (1 - fy) + r21 * fx * (1 - fy) + r12 * (1 - fx) * fy + r22 * fx * fy);
    uint8_t g = (uint8_t)(g11 * (1 - fx) * (1 - fy) + g21 * fx * (1 - fy) + g12 * (1 - fx) * fy + g22 * fx * fy);
    uint8_t b = (uint8_t)(b11 * (1 - fx) * (1 - fy) + b21 * fx * (1 - fy) + b12 * (1 - fx) * fy + b22 * fx * fy);
    uint8_t a = (uint8_t)(a11 * (1 - fx) * (1 - fy) + a21 * fx * (1 - fy) + a12 * (1 - fx) * fy + a22 * fx * fy);

    return r | (g << 8) | (b << 16) | (a << 24);
}

// Enhanced blit function with support for different scaling modes
void blit_surface_scaled(ImageSurface* src, Rect* src_rect, ImageSurface* dst, Rect* dst_rect, Bound* clip, ScaleMode scale_mode) {
    Rect rect;
    if (!src || !dst || !dst_rect || !clip) return;
    if (!src->pixels) {
        log_error("blit_surface_scaled: src->pixels is NULL!");
        return;
    }
    if (!dst->pixels) {
        log_error("blit_surface_scaled: dst->pixels is NULL!");
        return;
    }
    if (!src_rect) { // use the entire source image
        rect = (Rect){0, 0, (float)src->width, (float)src->height};
        src_rect = &rect;
    }
    log_debug("blit surface: src(%f, %f, %f, %f) to dst(%f, %f, %f, %f), scale_mode=%d",
        src_rect->x, src_rect->y, src_rect->width, src_rect->height,
        dst_rect->x, dst_rect->y, dst_rect->width, dst_rect->height, scale_mode);

    float x_ratio = (float)src_rect->width / dst_rect->width;
    float y_ratio = (float)src_rect->height / dst_rect->height;
    int left = (int)std::max(clip->left, dst_rect->x);
    int right = (int)std::min(clip->right, dst_rect->x + dst_rect->width);
    int top = (int)std::max(clip->top, dst_rect->y);
    int bottom = (int)std::min(clip->bottom, dst_rect->y + dst_rect->height);
    if (left >= right || top >= bottom) return; // dst_rect outside the dst surface

    for (int i = top; i < bottom; i++) {
        uint8_t* row_pixels = (uint8_t*)dst->pixels + i * dst->pitch;
        for (int j = left; j < right; j++) {
            float src_x = src_rect->x + (j - dst_rect->x) * x_ratio;
            float src_y = src_rect->y + (i - dst_rect->y) * y_ratio;

            uint8_t* dst_pixel = (uint8_t*)row_pixels + (j * 4);

            uint32_t src_color;
            if (scale_mode == SCALE_MODE_LINEAR) {
                // Bilinear interpolation
                src_color = bilinear_interpolate(src, src_x, src_y);
            }
            else { // Nearest neighbor scaling (default)
                int int_src_x = (int)(src_x + 0.5f);  // round to nearest
                int int_src_y = (int)(src_y + 0.5f);

                // bounds check for source coordinates
                if (int_src_x < 0 || int_src_x >= src->width || int_src_y < 0 || int_src_y >= src->height) {
                    continue; // skip pixels outside source bounds
                }

                uint8_t* src_pixel = (uint8_t*)src->pixels + (int_src_y * src->pitch) + (int_src_x * 4);
                src_color = *((uint32_t*)src_pixel);
            }

            // Alpha blend: src over dst
            uint8_t src_r = src_color & 0xFF;
            uint8_t src_g = (src_color >> 8) & 0xFF;
            uint8_t src_b = (src_color >> 16) & 0xFF;
            uint8_t src_a = (src_color >> 24) & 0xFF;

            if (src_a == 255) {
                // Fully opaque - direct copy
                *((uint32_t*)dst_pixel) = src_color;
            } else if (src_a > 0) {
                // Partially transparent - blend with background
                uint8_t dst_r = dst_pixel[0];
                uint8_t dst_g = dst_pixel[1];
                uint8_t dst_b = dst_pixel[2];
                uint8_t dst_a = dst_pixel[3];

                // Alpha compositing: src over dst
                float alpha = src_a / 255.0f;
                float inv_alpha = 1.0f - alpha;

                dst_pixel[0] = (uint8_t)(src_r * alpha + dst_r * inv_alpha);
                dst_pixel[1] = (uint8_t)(src_g * alpha + dst_g * inv_alpha);
                dst_pixel[2] = (uint8_t)(src_b * alpha + dst_b * inv_alpha);
                dst_pixel[3] = (uint8_t)(src_a + dst_a * inv_alpha);
            }
            // if src_a == 0, skip (fully transparent)
        }
    }
}

void image_surface_destroy(ImageSurface* img_surface) {
    if (img_surface) {
        if (img_surface->pixels) mem_free(img_surface->pixels);
        if (img_surface->pic) {
            tvg_paint_unref(img_surface->pic, true);
        }
        mem_free(img_surface);
    }
}
