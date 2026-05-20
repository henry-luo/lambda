#include "view.hpp"
#include "rdt_vector.hpp"
#include "render_raster.hpp"
#include "clip_shape.h"
#include "gif_player.h"
#include "lottie_player.h"
#include "state_store.hpp"
#include "retained_fields.hpp"

#include "../lib/image.h"
#include "../lib/log.h"
#include "../lib/memtrack.h"
#include "../lib/base64.h"
#include "../lib/url.h"
#include "../lambda/input/input.hpp"  // for download_http_content
typedef struct ImageEntry {
    // ImageFormat format;
    const char* path;  // todo: change to URL
    ImageSurface *image;
} ImageEntry;

int image_compare(const void *a, const void *b, void *udata) {
    (void)udata;
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

static const char* find_bytes(const char* data, size_t size, const char* needle, size_t needle_len) {
    if (!data || !needle || needle_len == 0 || size < needle_len) return NULL;
    for (size_t i = 0; i <= size - needle_len; i++) {
        if (memcmp(data + i, needle, needle_len) == 0) return data + i;
    }
    return NULL;
}

static bool svg_has_intrinsic_size_in_memory(const char* data, size_t size) {
    if (!data || size == 0) return false;

    const char* end = data + size;
    const char* svg = find_bytes(data, size, "<svg", 4);
    if (!svg) return false;

    const char* tag_end = svg;
    while (tag_end < end && *tag_end != '>') tag_end++;
    if (tag_end >= end) return false;

    size_t tag_len = (size_t)(tag_end - svg);
    bool has_width = find_bytes(svg, tag_len, "width", 5) != NULL;
    bool has_height = find_bytes(svg, tag_len, "height", 6) != NULL;
    return has_width && has_height;
}

static bool svg_has_intrinsic_size_in_file(const char* file_path) {
    if (!file_path) return false;

    FILE* fp = fopen(file_path, "rb");
    if (!fp) return false;

    char buffer[4096];
    size_t read_count = fread(buffer, 1, sizeof(buffer), fp);
    fclose(fp);
    return svg_has_intrinsic_size_in_memory(buffer, read_count);
}

static uint16_t read_exif_u16(const unsigned char* p, bool little_endian) {
    if (little_endian) return (uint16_t)(p[0] | (p[1] << 8));
    return (uint16_t)((p[0] << 8) | p[1]);
}

static uint32_t read_exif_u32(const unsigned char* p, bool little_endian) {
    if (little_endian) {
        return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
               ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    }
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static int jpeg_exif_orientation_from_memory(const unsigned char* data, size_t size) {
    if (!data || size < 4 || data[0] != 0xFF || data[1] != 0xD8) return 1;

    size_t pos = 2;
    while (pos + 4 <= size) {
        while (pos < size && data[pos] == 0xFF) pos++;
        if (pos >= size) break;

        unsigned char marker = data[pos++];
        if (marker == 0xDA || marker == 0xD9) break;
        if (pos + 2 > size) break;

        uint16_t seg_len = (uint16_t)((data[pos] << 8) | data[pos + 1]);
        pos += 2;
        if (seg_len < 2) break;

        size_t payload_len = (size_t)seg_len - 2;
        if (pos + payload_len > size) break;

        if (marker == 0xE1 && payload_len >= 14 && memcmp(data + pos, "Exif\0\0", 6) == 0) {
            const unsigned char* tiff = data + pos + 6;
            size_t tiff_len = payload_len - 6;
            if (tiff_len < 8) return 1;

            bool little_endian = false;
            if (tiff[0] == 'I' && tiff[1] == 'I') little_endian = true;
            else if (tiff[0] == 'M' && tiff[1] == 'M') little_endian = false;
            else return 1;

            if (read_exif_u16(tiff + 2, little_endian) != 42) return 1;
            uint32_t ifd_offset = read_exif_u32(tiff + 4, little_endian);
            if (ifd_offset + 2 > tiff_len) return 1;

            const unsigned char* ifd = tiff + ifd_offset;
            uint16_t entry_count = read_exif_u16(ifd, little_endian);
            size_t entries_start = ifd_offset + 2;
            for (uint16_t i = 0; i < entry_count; i++) {
                size_t entry_offset = entries_start + (size_t)i * 12;
                if (entry_offset + 12 > tiff_len) break;

                const unsigned char* entry = tiff + entry_offset;
                uint16_t tag = read_exif_u16(entry, little_endian);
                uint16_t type = read_exif_u16(entry + 2, little_endian);
                uint32_t count = read_exif_u32(entry + 4, little_endian);
                if (tag == 0x0112 && type == 3 && count >= 1) {
                    int orientation = read_exif_u16(entry + 8, little_endian);
                    return (orientation >= 1 && orientation <= 8) ? orientation : 1;
                }
            }
            return 1;
        }
        pos += payload_len;
    }
    return 1;
}

static int jpeg_exif_orientation_from_file(const char* file_path) {
    if (!file_path) return 1;

    FILE* fp = fopen(file_path, "rb");
    if (!fp) return 1;

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return 1;
    }
    long file_size = ftell(fp);
    if (file_size <= 0) {
        fclose(fp);
        return 1;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return 1;
    }

    unsigned char* bytes = (unsigned char*)mem_alloc((size_t)file_size, MEM_CAT_IMAGE);
    if (!bytes) {
        fclose(fp);
        return 1;
    }
    size_t read_count = fread(bytes, 1, (size_t)file_size, fp);
    fclose(fp);

    int orientation = 1;
    if (read_count == (size_t)file_size) {
        orientation = jpeg_exif_orientation_from_memory(bytes, (size_t)file_size);
    }
    mem_free(bytes);
    return orientation;
}

static void image_surface_apply_orientation_metadata(ImageSurface* surface, int orientation) {
    if (!surface) return;

    surface->encoded_width = surface->width;
    surface->encoded_height = surface->height;
    surface->orientation = (orientation >= 1 && orientation <= 8) ? orientation : 1;
    surface->has_intrinsic_size = true;
    if (surface->orientation >= 5 && surface->orientation <= 8) {
        int width = surface->width;
        surface->width = surface->height;
        surface->height = width;
    }
}

ImageSurface* load_image(UiContext* uicon, const char *img_url) {
    if (uicon->document == NULL || uicon->document->url == NULL) {
        log_error("Missing URL context for image: %s", img_url);
        return NULL;
    }

    // Handle data: URIs
    if (strncmp(img_url, "data:", 5) == 0) {
        const char* comma = strchr(img_url, ',');
        if (!comma) {
            log_error("[BG-IMAGE] Invalid data URI (no comma)");
            return NULL;
        }

        // Check if data URI is base64-encoded: "data:...;base64,..."
        bool is_base64 = false;
        const char* meta = img_url + 5;  // after "data:"
        size_t meta_len = comma - meta;
        for (size_t i = 0; i + 5 < meta_len; i++) {
            if (strncasecmp(meta + i, "base64", 6) == 0) {
                is_base64 = true;
                break;
            }
        }

        size_t decoded_len = 0;
        uint8_t* decoded = NULL;
        if (is_base64) {
            decoded = base64_decode(comma + 1, 0, &decoded_len);
        } else {
            // URL-encoded (percent-encoded) data URI
            const char* data_str = comma + 1;
            size_t data_str_len = strlen(data_str);
            decoded = (uint8_t*)url_decode_component(data_str, data_str_len, &decoded_len);
        }
        if (!decoded || decoded_len == 0) {
            log_error("[BG-IMAGE] Failed to decode data URI");
            return NULL;
        }
        // Detect format from MIME type or content
        bool is_svg = is_svg_content(decoded, decoded_len);
        ImageSurface* surface;
        if (is_svg) {
            surface = (ImageSurface*)mem_calloc(1, sizeof(ImageSurface), MEM_CAT_IMAGE);
            surface->format = IMAGE_FORMAT_SVG;
            surface->pic = rdt_picture_load_data((const char*)decoded, (int)decoded_len, "svg");
            mem_free(decoded);
            if (!surface->pic) {
                mem_free(surface);
                return NULL;
            }
            float svg_w, svg_h;
            rdt_picture_get_size(surface->pic, &svg_w, &svg_h);
            surface->width = svg_w;
            surface->height = svg_h;
            image_surface_apply_orientation_metadata(surface, 1);
            surface->has_intrinsic_size = svg_has_intrinsic_size_in_memory((const char*)decoded, decoded_len);
        } else {
            int width, height, channels;
            int orientation = jpeg_exif_orientation_from_memory(decoded, decoded_len);
            unsigned char* data = image_load_from_memory(decoded, decoded_len, &width, &height, &channels);
            mem_free(decoded);
            if (!data) {
                log_error("[BG-IMAGE] Failed to decode data URI image");
                return NULL;
            }
            surface = image_surface_create_from(width, height, data);
            if (!surface) { image_free(data); return NULL; }
            // Detect format from MIME
            if (strstr(img_url, "image/png")) surface->format = IMAGE_FORMAT_PNG;
            else if (strstr(img_url, "image/jpeg") || strstr(img_url, "image/jpg")) surface->format = IMAGE_FORMAT_JPEG;
            else if (strstr(img_url, "image/gif")) surface->format = IMAGE_FORMAT_GIF;
            else if (strstr(img_url, "image/svg")) surface->format = IMAGE_FORMAT_SVG;
            if (surface->format == IMAGE_FORMAT_JPEG) {
                image_surface_apply_orientation_metadata(surface, orientation);
            } else {
                image_surface_apply_orientation_metadata(surface, 1);
            }
        }
        log_debug("[BG-IMAGE] Loaded data URI image: %dx%d", surface->width, surface->height);
        return surface;
    }
    Url* abs_url = parse_url(uicon->document->url, img_url);
    if (!abs_url) {
        log_error("Failed to parse URL: %s", img_url);
        return NULL;
    }

    // Check if this is an HTTP URL
    bool is_http = (abs_url->scheme == URL_SCHEME_HTTP || abs_url->scheme == URL_SCHEME_HTTPS);
    char* file_path = nullptr;
    unsigned char* downloaded_data = nullptr;
    size_t downloaded_size = 0;

    if (is_http) {
        // When network resource manager is active, images are loaded asynchronously.
        // Return NULL so layout uses placeholder sizing and reflows when image arrives.
        if (uicon->document->resource_manager) {
            log_debug("[image] Skipping sync download (resource manager active): %s", url_get_href(abs_url));
            url_destroy(abs_url);
            return NULL;
        }
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
        // strdup to take ownership: entry->path must always be a malloc'd string
        // (url_get_href returns a pointer into the Url struct, not a separate allocation)
        file_path = mem_strdup(url_str, MEM_CAT_RENDER);
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
        mem_free(file_path);  // always malloc-owned: strdup for HTTP, url_to_local_path for local
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
        if (is_http && downloaded_data) {
            surface->pic = rdt_picture_load_data((const char*)downloaded_data, (int)downloaded_size, "svg");
        } else {
            surface->pic = rdt_picture_load(file_path);
        }
        if (!surface->pic) {
            log_debug("failed to load SVG image: %s", file_path);
            mem_free(surface);
            if (downloaded_data) mem_free(downloaded_data);
            return NULL;
        }
        float svg_w, svg_h;
        rdt_picture_get_size(surface->pic, &svg_w, &svg_h);
        surface->width = svg_w;
        surface->height = svg_h;
        image_surface_apply_orientation_metadata(surface, 1);
        surface->has_intrinsic_size = is_http && downloaded_data
            ? svg_has_intrinsic_size_in_memory((const char*)downloaded_data, downloaded_size)
            : svg_has_intrinsic_size_in_file(file_path);
        log_debug("SVG image size: %f x %f\n", svg_w, svg_h);
        if (downloaded_data) mem_free(downloaded_data);
    }
    // Detect Lottie JSON animation (by extension for local, by content for HTTP)
    else if ((!is_http && lottie_detect_by_path(file_path)) ||
             (is_http && downloaded_data && lottie_detect_by_content(downloaded_data, downloaded_size))) {
        // Create a placeholder surface — pixels will be filled by the LottiePlayer
        surface = (ImageSurface*)mem_calloc(1, sizeof(ImageSurface), MEM_CAT_IMAGE);
        // Try to get natural dimensions from the Lottie via ThorVG picture
        // For now, use a default render size; the layout will resize as needed
        surface->width = 300;   // default Lottie render width
        surface->height = 300;  // default Lottie render height
        surface->format = IMAGE_FORMAT_SVG;  // treat as vector-like for rendering

        // Register with animation scheduler if available
        if (uicon->document && uicon->document->state) {
            DocState* rs = (DocState*)uicon->document->state;
            if (rs && rs->animation_scheduler) {
                AnimationInstance* inst = NULL;
                if (is_http && downloaded_data) {
                    inst = lottie_player_create_from_data(rs->animation_scheduler, surface,
                                (const char*)downloaded_data, downloaded_size,
                                surface->width, surface->height,
                                rs->animation_scheduler->current_time, uicon->document->pool);
                } else {
                    inst = lottie_player_create_from_file(rs->animation_scheduler, surface,
                                file_path, surface->width, surface->height,
                                rs->animation_scheduler->current_time, uicon->document->pool);
                }
                if (inst) {
                    LottiePlayer* lp = (LottiePlayer*)inst->state;
                    if (lp) {
                        surface->width = lp->width;
                        surface->height = lp->height;
                    }
                    log_info("lottie animated: registered with scheduler from %s", file_path);
                } else {
                    // Not a valid Lottie — fall through to raster path is not possible here
                    // Free and return NULL
                    log_debug("lottie detect: failed to load as Lottie: %s", file_path);
                    mem_free(surface);
                    surface = NULL;
                }
            }
        }
        if (downloaded_data) mem_free(downloaded_data);
        if (!surface) return NULL;
        image_surface_apply_orientation_metadata(surface, 1);
    }
    else {
        int width, height;
        if (is_http && downloaded_data) {
            // HTTP images: read dimensions from memory header, keep data for lazy decode
            if (image_get_dimensions_from_memory(downloaded_data, downloaded_size, &width, &height)) {
                surface = (ImageSurface*)mem_calloc(1, sizeof(ImageSurface), MEM_CAT_IMAGE);
                surface->width = width;
                surface->height = height;
                {
                    lam::SessionPtr<unsigned char> source_data = lam::take_ownership(downloaded_data);
                    radiant_take_image_source_data(surface, source_data, downloaded_size);
                }
                downloaded_data = nullptr;
                // pixels stays NULL — decoded on demand
                log_debug("[image] Lazy load HTTP image: %dx%d (%zu bytes)", width, height, downloaded_size);
            } else {
                // Fallback: full decode if header read fails
                int channels;
                unsigned char *data = image_load_from_memory(downloaded_data, downloaded_size, &width, &height, &channels);
                mem_free(downloaded_data);
                if (!data) {
                    log_debug("failed to load image: %s", file_path);
                    return NULL;
                }
                surface = image_surface_create_from(width, height, data);
                if (!surface) { image_free(data); return NULL; }
            }
        } else {
            // Local files: read dimensions from file header only
            if (image_get_dimensions(file_path, &width, &height)) {
                surface = (ImageSurface*)mem_calloc(1, sizeof(ImageSurface), MEM_CAT_IMAGE);
                surface->width = width;
                surface->height = height;
                {
                    lam::SessionPtr<char> source_path = lam::session_strdup(file_path, MEM_CAT_IMAGE);
                    radiant_take_image_source_path(surface, source_path);
                }
                // pixels stays NULL — decoded on demand
                log_debug("[image] Lazy load local image: %dx%d from %s", width, height, file_path);
            } else {
                // Fallback: full decode if header read fails
                int channels;
                unsigned char *data = image_load(file_path, &width, &height, &channels, 4);
                if (!data) {
                    log_debug("failed to load image: %s", file_path);
                    return NULL;
                }
                surface = image_surface_create_from(width, height, data);
                if (!surface) { image_free(data); return NULL; }
            }
        }
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
        if (surface->format == IMAGE_FORMAT_JPEG) {
            int orientation = 1;
            if (is_http && surface->source_data && surface->source_data_len > 0) {
                orientation = jpeg_exif_orientation_from_memory(surface->source_data, surface->source_data_len);
            } else {
                orientation = jpeg_exif_orientation_from_file(file_path);
            }
            image_surface_apply_orientation_metadata(surface, orientation);
            log_debug("[image] JPEG orientation: exif=%d encoded=%dx%d natural=%dx%d",
                      surface->orientation, surface->encoded_width, surface->encoded_height,
                      surface->width, surface->height);
        } else {
            image_surface_apply_orientation_metadata(surface, 1);
        }
    }
    surface->url = abs_url;

    // Detect animated GIF and register with animation scheduler
    if (surface->format == IMAGE_FORMAT_GIF && uicon->document && uicon->document->state) {
        GifFrames* gif_frames = NULL;
        if (surface->source_path) {
            gif_frames = gif_detect_animated(surface->source_path);
        } else if (surface->source_data && surface->source_data_len > 0) {
            gif_frames = gif_detect_animated_from_memory(surface->source_data, surface->source_data_len);
        }
        if (gif_frames) {
            DocState* rs = (DocState*)uicon->document->state;
            if (rs && rs->animation_scheduler) {
                gif_animation_create(rs->animation_scheduler, surface, gif_frames,
                                      rs->animation_scheduler->current_time, uicon->document->pool);
                log_info("gif animated: registered %d-frame GIF with scheduler", gif_frames->frame_count);
            } else {
                image_gif_free(gif_frames);
            }
        }
    }

    ImageEntry new_entry = {.path = (char*)file_path, .image = surface};
    hashmap_set(uicon->image_cache, &new_entry);
    return surface;
}

bool image_entry_free(const void *item, void *udata) {
    (void)udata;
    ImageEntry* entry = (ImageEntry*)item;
    mem_free((char*)entry->path);  // always mem_alloc-owned: mem_strdup for HTTP paths, url_to_local_path for local paths
    if (entry->image->url) url_destroy(entry->image->url);
    image_surface_destroy(entry->image);
    return true;
}

void image_cache_cleanup(UiContext* uicon) {
    // loop through the hashmap and free the images
    if (uicon->image_cache) {
        log_debug("Cleaning up cached images");
        hashmap_scan(uicon->image_cache, image_entry_free, NULL);
        hashmap_free(uicon->image_cache);
        uicon->image_cache = NULL;
    }
}

ImageSurface* image_surface_create(int pixel_width, int pixel_height) {
    if (pixel_width <= 0 || pixel_height <= 0) {
        log_error("[surface] Invalid image surface dimensions");
        return NULL;
    }
    ImageSurface* img_surface = (ImageSurface*)mem_calloc(1, sizeof(ImageSurface), MEM_CAT_IMAGE);
    if (!img_surface) {
        log_error("[surface] Could not allocate image surface");
        return NULL;
    }
    img_surface->width = pixel_width;  img_surface->height = pixel_height;
    img_surface->encoded_width = pixel_width;  img_surface->encoded_height = pixel_height;
    img_surface->orientation = 1;
    img_surface->has_intrinsic_size = true;
    img_surface->pitch = pixel_width * 4;
    img_surface->generation = 1;
    img_surface->pixels = mem_calloc(pixel_width * pixel_height * 4, sizeof(uint32_t), MEM_CAT_IMAGE);
    if (!img_surface->pixels) {
        log_error("[surface] Could not allocate memory for image surface");
        mem_free(img_surface);
        return NULL;
    }
    return img_surface;
}

ImageSurface* image_surface_create_from(int pixel_width, int pixel_height, void* pixels) {
    if (pixel_width <= 0 || pixel_height <= 0 || !pixels) {
        log_error("[surface] Invalid image surface dimensions or pixels");
        return NULL;
    }
    ImageSurface* img_surface = (ImageSurface*)mem_calloc(1, sizeof(ImageSurface), MEM_CAT_IMAGE);
    if (img_surface) {
        img_surface->width = pixel_width;  img_surface->height = pixel_height;
        img_surface->encoded_width = pixel_width;  img_surface->encoded_height = pixel_height;
        img_surface->orientation = 1;
        img_surface->has_intrinsic_size = true;
        img_surface->pitch = pixel_width * 4;
        img_surface->pixels = pixels;
        img_surface->generation = 1;
    }
    return img_surface;
}

void fill_surface_rect(ImageSurface* surface, Rect* rect, uint32_t color, Bound* clip,
                       ClipShape** clip_shapes, int clip_depth) {
    RasterPaintContext ctx = {surface, clip, clip_shapes, clip_depth};
    raster_fill_rect(&ctx, rect, color);
}

// Enhanced blit function with support for different scaling modes
void blit_surface_scaled(ImageSurface* src, Rect* src_rect, ImageSurface* dst, Rect* dst_rect, Bound* clip, ScaleMode scale_mode,
                         ClipShape** clip_shapes, int clip_depth) {
    RasterPaintContext ctx = {dst, clip, clip_shapes, clip_depth};
    raster_blit_surface_scaled(&ctx, src, src_rect, dst_rect, scale_mode, 255);
}

void image_surface_destroy(ImageSurface* img_surface) {
    if (img_surface) {
        if (img_surface->pixels) mem_free(img_surface->pixels);
        if (img_surface->pic) {
            rdt_picture_free(img_surface->pic);
        }
        if (img_surface->source_path) mem_free(img_surface->source_path);
        if (img_surface->source_data) mem_free(img_surface->source_data);
        mem_free(img_surface);
    }
}

static bool image_surface_can_promote_decode(ImageSurface* img, int target_w, int target_h) {
    if (!img || !img->pixels) return true;
    if (img->format != IMAGE_FORMAT_PNG && img->format != IMAGE_FORMAT_JPEG) return false;
    if (!img->source_path && !img->source_data) return false;
    int decoded_w = img->decoded_width > 0 ? img->decoded_width : img->width;
    int decoded_h = img->decoded_height > 0 ? img->decoded_height : img->height;
    if (target_w <= 0) target_w = decoded_w;
    if (target_h <= 0) target_h = decoded_h;
    return target_w > decoded_w || target_h > decoded_h;
}

void image_surface_ensure_decoded(ImageSurface* img, int target_w, int target_h) {
    if (!img) return;

    // Clamp targets to a sensible minimum to avoid degenerate 0-pixel decodes.
    if (target_w < 0) target_w = 0;
    if (target_h < 0) target_h = 0;
    if (!image_surface_can_promote_decode(img, target_w, target_h)) return;

    if (img->source_path) {
        // decode from local file
        int width, height, channels;
        unsigned char* data = image_load_scaled(img->source_path, target_w, target_h, &width, &height, &channels);
        if (data) {
            if (img->pixels) {
                mem_free(img->pixels);
            }
            img->pixels = data;
            // record actual decoded buffer dims; intrinsic width/height stay unchanged for layout.
            img->decoded_width = width;
            img->decoded_height = height;
            img->pitch = width * 4;
            image_surface_bump_generation(img);
            log_debug("[image] Decoded local image on demand: %dx%d (intrinsic %dx%d, target %dx%d) from %s",
                      width, height, img->width, img->height, target_w, target_h, img->source_path);
        } else {
            log_error("[image] Failed to decode local image: %s", img->source_path);
        }
    } else if (img->source_data) {
        // decode from memory buffer
        int width, height, channels;
        unsigned char* data = image_load_from_memory_scaled(img->source_data, img->source_data_len,
                                                            target_w, target_h, &width, &height, &channels);
        if (data) {
            if (img->pixels) {
                mem_free(img->pixels);
            }
            img->pixels = data;
            img->decoded_width = width;
            img->decoded_height = height;
            img->pitch = width * 4;
            image_surface_bump_generation(img);
            log_debug("[image] Decoded HTTP image on demand: %dx%d (intrinsic %dx%d, target %dx%d)",
                      width, height, img->width, img->height, target_w, target_h);
        } else {
            log_error("[image] Failed to decode HTTP image from memory");
        }
        if (img->decoded_width >= img->width && img->decoded_height >= img->height) {
            mem_free(img->source_data);
            radiant_clear_image_source_data(img);
        }
    }
}
