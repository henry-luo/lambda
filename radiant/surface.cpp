#include "view.hpp"
#include "rdt_vector.hpp"
#include "clip_shape.h"
#include "gif_player.h"
#include "lottie_player.h"
#include "state_store.hpp"

#include "../lib/image.h"
#include "../lib/log.h"
#include "../lib/memtrack.h"
#include "../lib/base64.h"
#include "../lib/url.h"
#include "../lambda/input/input.hpp"  // for download_http_content
#include <algorithm>  // for std::max, std::min
#include <stdio.h>
#include <ctype.h>

// ThorVG's internal SVG loader matches fonts by family-name lookup only and
// ignores font-weight.  When SVG declares font-family="Foo" font-weight="bold",
// ThorVG renders with the registered "Foo" (regular) — never picking up
// "Foo Bold" even when both are loaded via tvg_font_load.  Browsers, by
// contrast, resolve weighted families to the appropriate face.
//
// This pre-processor scans the SVG text and, when an element/attribute pair
// requests bold (font-weight value >= 600 or "bold"/"bolder"), rewrites the
// nearest font-family attribute on the same element to "<First Name> Bold".
// The result is loaded via rdt_picture_load_data so ThorVG sees the bold
// face name directly.  Returns a newly mem_alloc'd buffer (or NULL if no
// rewrite was needed).
static uint8_t* preprocess_svg_bold_fonts(const uint8_t* data, size_t len, size_t* out_len) {
    if (!data || len == 0) return NULL;
    // quick check: bail if no font-weight attribute mentions bold/600+
    const char* sdata = (const char*)data;
    bool needs_rewrite = false;
    for (size_t i = 0; i + 12 < len; i++) {
        if (strncasecmp(sdata + i, "font-weight", 11) != 0) continue;
        size_t j = i + 11;
        while (j < len && (sdata[j] == ' ' || sdata[j] == '\t' || sdata[j] == ':' || sdata[j] == '=')) j++;
        if (j >= len) break;
        char q = sdata[j];
        const char* val_start;
        const char* val_end;
        if (q == '"' || q == '\'') {
            val_start = sdata + j + 1;
            val_end = (const char*)memchr(val_start, q, len - (val_start - sdata));
            if (!val_end) break;
        } else {
            val_start = sdata + j;
            val_end = val_start;
            while (val_end < sdata + len && *val_end != ';' && *val_end != ' ' &&
                   *val_end != '"' && *val_end != '>' && *val_end != '/' && *val_end != '\n' &&
                   *val_end != '\t' && *val_end != ',') val_end++;
        }
        size_t vlen = val_end - val_start;
        if (vlen == 0) continue;
        if ((vlen == 4 && strncasecmp(val_start, "bold", 4) == 0) ||
            (vlen == 6 && strncasecmp(val_start, "bolder", 6) == 0)) {
            needs_rewrite = true; break;
        }
        // numeric weight
        char buf[8] = {0};
        size_t cp = vlen < 7 ? vlen : 7;
        memcpy(buf, val_start, cp);
        int n = atoi(buf);
        if (n >= 600) { needs_rewrite = true; break; }
    }
    if (!needs_rewrite) return NULL;

    // Build new buffer.  For each element-tag region (between '<' and '>') that
    // contains a bold weight, rewrite the first font-family value to append
    // " Bold" to its first family name.  Operates on attribute values only.
    char* out = (char*)mem_alloc(len * 2 + 64, MEM_CAT_RENDER);
    if (!out) return NULL;
    size_t op = 0;

    size_t i = 0;
    while (i < len) {
        if (sdata[i] != '<') { out[op++] = sdata[i++]; continue; }
        // find matching '>'
        size_t tag_start = i;
        size_t tag_end = i + 1;
        while (tag_end < len && sdata[tag_end] != '>') tag_end++;
        if (tag_end >= len) { memcpy(out + op, sdata + i, len - i); op += len - i; break; }
        size_t tag_len = tag_end - tag_start + 1;

        // search for font-weight inside this tag
        bool tag_is_bold = false;
        const char* trange = sdata + tag_start;
        for (size_t k = 0; k + 11 < tag_len; k++) {
            if (strncasecmp(trange + k, "font-weight", 11) != 0) continue;
            size_t m = k + 11;
            while (m < tag_len && (trange[m] == ' ' || trange[m] == ':' || trange[m] == '=' || trange[m] == '\t')) m++;
            if (m >= tag_len) break;
            char q = trange[m];
            const char* vs;
            const char* ve;
            if (q == '"' || q == '\'') {
                vs = trange + m + 1;
                ve = (const char*)memchr(vs, q, tag_len - (vs - trange));
                if (!ve) break;
            } else {
                vs = trange + m;
                ve = vs;
                while (ve < trange + tag_len && *ve != ';' && *ve != ' ' &&
                       *ve != '"' && *ve != '>' && *ve != '/' && *ve != '\n' &&
                       *ve != '\t' && *ve != ',') ve++;
            }
            size_t vlen = ve - vs;
            if (vlen == 0) continue;
            if ((vlen == 4 && strncasecmp(vs, "bold", 4) == 0) ||
                (vlen == 6 && strncasecmp(vs, "bolder", 6) == 0)) {
                tag_is_bold = true;
            } else {
                char buf[8] = {0};
                size_t cp = vlen < 7 ? vlen : 7;
                memcpy(buf, vs, cp);
                if (atoi(buf) >= 600) tag_is_bold = true;
            }
            break;
        }

        if (!tag_is_bold) {
            memcpy(out + op, sdata + tag_start, tag_len);
            op += tag_len;
            i = tag_end + 1;
            continue;
        }

        // rewrite font-family inside this tag: append " Bold" to first family name
        size_t cursor = 0;
        bool rewritten = false;
        while (cursor < tag_len) {
            if (cursor + 11 < tag_len && strncasecmp(trange + cursor, "font-family", 11) == 0) {
                // copy bytes BEFORE the font-family attribute (e.g., '<text x="..." ')
                memcpy(out + op, trange, cursor);
                op += cursor;
                // copy attribute name + whitespace/colon/equals up to the value
                size_t n = cursor + 11;
                while (n < tag_len && (trange[n] == ' ' || trange[n] == ':' || trange[n] == '=' || trange[n] == '\t')) n++;
                if (n >= tag_len) {
                    memcpy(out + op, trange + cursor, tag_len - cursor);
                    op += tag_len - cursor;
                    rewritten = true;
                    break;
                }
                memcpy(out + op, trange + cursor, n - cursor);
                op += n - cursor;
                char q = trange[n];
                if (q == '"' || q == '\'') {
                    out[op++] = q;
                    n++;
                }
                // first family name = up to ',' or closing quote/space
                const char* vs = trange + n;
                const char* ve = vs;
                while (ve < trange + tag_len) {
                    char c = *ve;
                    if (c == ',' || c == q || (q != '"' && q != '\'' && (c == ' ' || c == ';' || c == '>' || c == '/'))) break;
                    ve++;
                }
                // strip trailing whitespace from first family name
                const char* ne = ve;
                while (ne > vs && (ne[-1] == ' ' || ne[-1] == '\t')) ne--;
                size_t fam_len = ne - vs;
                memcpy(out + op, vs, fam_len);
                op += fam_len;
                // append " Bold" if not already
                if (fam_len < 5 || strncasecmp(out + op - 5, " Bold", 5) != 0) {
                    memcpy(out + op, " Bold", 5);
                    op += 5;
                }
                // copy the rest of the value + tag
                size_t rest_off = ve - trange;
                memcpy(out + op, trange + rest_off, tag_len - rest_off);
                op += tag_len - rest_off;
                rewritten = true;
                break;
            }
            cursor++;
        }
        if (!rewritten) {
            // no font-family attribute found in this bold tag — copy verbatim
            memcpy(out + op, trange, tag_len);
            op += tag_len;
        }
        i = tag_end + 1;
    }

    if (out_len) *out_len = op;
    return (uint8_t*)out;
}

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
        } else {
            int width, height, channels;
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
    char* temp_file_path = nullptr;
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
            size_t pre_len = 0;
            uint8_t* pre = preprocess_svg_bold_fonts(downloaded_data, downloaded_size, &pre_len);
            if (pre) {
                surface->pic = rdt_picture_load_data((const char*)pre, (int)pre_len, "svg");
                mem_free(pre);
            } else {
                surface->pic = rdt_picture_load_data((const char*)downloaded_data, (int)downloaded_size, "svg");
            }
        } else {
            // read file, preprocess for bold-font rewrite, load via load_data
            FILE* fp = fopen(file_path, "rb");
            if (fp) {
                fseek(fp, 0, SEEK_END);
                long fsz = ftell(fp);
                fseek(fp, 0, SEEK_SET);
                if (fsz > 0) {
                    uint8_t* fbuf = (uint8_t*)mem_alloc((size_t)fsz, MEM_CAT_RENDER);
                    size_t rd = fread(fbuf, 1, (size_t)fsz, fp);
                    fclose(fp);
                    size_t pre_len = 0;
                    uint8_t* pre = preprocess_svg_bold_fonts(fbuf, rd, &pre_len);
                    if (pre) {
                        log_debug("SVG bold-font rewrite applied: %s", file_path);
                        surface->pic = rdt_picture_load_data((const char*)pre, (int)pre_len, "svg");
                        mem_free(pre);
                    } else {
                        surface->pic = rdt_picture_load(file_path);
                    }
                    mem_free(fbuf);
                } else {
                    fclose(fp);
                    surface->pic = rdt_picture_load(file_path);
                }
            } else {
                surface->pic = rdt_picture_load(file_path);
            }
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
            RadiantState* rs = (RadiantState*)uicon->document->state;
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
    }
    else {
        int width, height;
        if (is_http && downloaded_data) {
            // HTTP images: read dimensions from memory header, keep data for lazy decode
            if (image_get_dimensions_from_memory(downloaded_data, downloaded_size, &width, &height)) {
                surface = (ImageSurface*)mem_calloc(1, sizeof(ImageSurface), MEM_CAT_IMAGE);
                surface->width = width;
                surface->height = height;
                surface->source_data = downloaded_data;
                surface->source_data_len = downloaded_size;
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
                surface->source_path = mem_strdup(file_path, MEM_CAT_IMAGE);
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
            RadiantState* rs = (RadiantState*)uicon->document->state;
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

void fill_surface_rect(ImageSurface* surface, Rect* rect, uint32_t color, Bound* clip,
                       ClipShape** clip_shapes, int clip_depth) {
    Rect r;
    if (!surface || !surface->pixels) return;
    if (!rect) { r = (Rect){0, 0, (float)surface->width, (float)surface->height};  rect = &r; }
    log_debug("fill rect: x:%.0f, y:%.0f, wd:%.0f, hg:%.0f, color:%x", rect->x, rect->y, rect->width, rect->height, color);

    // Pixel-snap: round edges to nearest pixel for subpixel accuracy
    int left = (int)roundf(std::max(clip->left, rect->x));
    int right = (int)roundf(std::min(clip->right, rect->x + rect->width));
    int top = (int)roundf(std::max(clip->top, rect->y));
    int bottom = (int)roundf(std::min(clip->bottom, rect->y + rect->height));
    if (left >= right || top >= bottom) return; // rect outside clip

    int y_off = surface->tile_offset_y;  // subtract to get tile-relative row index
    // Fast path: no clip shapes active
    if (clip_depth <= 0) {
        for (int i = top; i < bottom; i++) {
            uint8_t* row_pixels = (uint8_t*)surface->pixels + (i - y_off) * surface->pitch;
            _fill_row(row_pixels, left, right - left, color);
        }
        return;
    }

    // Check if rect is entirely inside all clip shapes
    if (clip_shapes_rect_inside(clip_shapes, clip_depth,
            (float)left + 0.5f, (float)top + 0.5f,
            (float)(right - left - 1), (float)(bottom - top - 1))) {
        for (int i = top; i < bottom; i++) {
            uint8_t* row_pixels = (uint8_t*)surface->pixels + (i - y_off) * surface->pitch;
            _fill_row(row_pixels, left, right - left, color);
        }
        return;
    }

    // Per-row scanline clipping
    for (int i = top; i < bottom; i++) {
        float py = (float)i + 0.5f;
        int rl = left, rr = right;
        clip_shapes_scanline_bounds(clip_shapes, clip_depth, py, left, right, &rl, &rr);
        if (rl >= rr) continue;
        uint8_t* row_pixels = (uint8_t*)surface->pixels + (i - y_off) * surface->pitch;
        _fill_row(row_pixels, rl, rr - rl, color);
    }
}

// Bilinear interpolation helper function (for upscaling or 1:1)
static uint32_t bilinear_interpolate_wrap(ImageSurface* src, float src_x, float src_y) {
    int w = src->width;
    int h = src->height;
    int x1 = (int)floorf(src_x);
    int y1 = (int)floorf(src_y);
    int x2 = x1 + 1;
    int y2 = y1 + 1;

    // wrap coordinates using modulo
    x1 = ((x1 % w) + w) % w;
    y1 = ((y1 % h) + h) % h;
    x2 = ((x2 % w) + w) % w;
    y2 = ((y2 % h) + h) % h;

    float fx = src_x - floorf(src_x);
    float fy = src_y - floorf(src_y);

    uint32_t* p11 = (uint32_t*)((uint8_t*)src->pixels + y1 * src->pitch + x1 * 4);
    uint32_t* p21 = (uint32_t*)((uint8_t*)src->pixels + y1 * src->pitch + x2 * 4);
    uint32_t* p12 = (uint32_t*)((uint8_t*)src->pixels + y2 * src->pitch + x1 * 4);
    uint32_t* p22 = (uint32_t*)((uint8_t*)src->pixels + y2 * src->pitch + x2 * 4);

    uint8_t r11 = *p11 & 0xFF, g11 = (*p11 >> 8) & 0xFF, b11 = (*p11 >> 16) & 0xFF, a11 = (*p11 >> 24) & 0xFF;
    uint8_t r21 = *p21 & 0xFF, g21 = (*p21 >> 8) & 0xFF, b21 = (*p21 >> 16) & 0xFF, a21 = (*p21 >> 24) & 0xFF;
    uint8_t r12 = *p12 & 0xFF, g12 = (*p12 >> 8) & 0xFF, b12 = (*p12 >> 16) & 0xFF, a12 = (*p12 >> 24) & 0xFF;
    uint8_t r22 = *p22 & 0xFF, g22 = (*p22 >> 8) & 0xFF, b22 = (*p22 >> 16) & 0xFF, a22 = (*p22 >> 24) & 0xFF;

    uint8_t r = (uint8_t)(r11 * (1 - fx) * (1 - fy) + r21 * fx * (1 - fy) + r12 * (1 - fx) * fy + r22 * fx * fy);
    uint8_t g = (uint8_t)(g11 * (1 - fx) * (1 - fy) + g21 * fx * (1 - fy) + g12 * (1 - fx) * fy + g22 * fx * fy);
    uint8_t b = (uint8_t)(b11 * (1 - fx) * (1 - fy) + b21 * fx * (1 - fy) + b12 * (1 - fx) * fy + b22 * fx * fy);
    uint8_t a = (uint8_t)(a11 * (1 - fx) * (1 - fy) + a21 * fx * (1 - fy) + a12 * (1 - fx) * fy + a22 * fx * fy);

    return r | (g << 8) | (b << 16) | (a << 24);
}

static uint32_t bilinear_interpolate(ImageSurface* src, float src_x, float src_y) {
    int x1 = (int)floorf(src_x);
    int y1 = (int)floorf(src_y);
    int x2 = x1 + 1;
    int y2 = y1 + 1;

    // clamp coordinates to source bounds
    x1 = std::max(0, std::min(x1, src->width - 1));
    y1 = std::max(0, std::min(y1, src->height - 1));
    x2 = std::max(0, std::min(x2, src->width - 1));
    y2 = std::max(0, std::min(y2, src->height - 1));

    float fx = src_x - floorf(src_x);
    float fy = src_y - floorf(src_y);

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

// Area averaging helper for downscaling: averages all source pixels in the box [x0,x1) x [y0,y1)
// This produces much better quality than bilinear when shrinking by >2x
static uint32_t area_average(ImageSurface* src, float x0, float y0, float x1, float y1) {
    // clamp to source bounds
    int ix0 = std::max(0, (int)x0);
    int iy0 = std::max(0, (int)y0);
    int ix1 = std::min(src->width, (int)(x1 + 1.0f));
    int iy1 = std::min(src->height, (int)(y1 + 1.0f));

    if (ix0 >= ix1 || iy0 >= iy1) return 0;

    float sum_r = 0, sum_g = 0, sum_b = 0, sum_a = 0;
    float total_weight = 0;

    for (int y = iy0; y < iy1; y++) {
        // compute vertical weight: fraction of this row that falls within [y0, y1]
        float wy = 1.0f;
        if (y < y0) wy = 1.0f - (y0 - y);        // partial top row
        if (y + 1 > y1) wy = y1 - y;              // partial bottom row
        if (wy <= 0) continue;

        uint8_t* row = (uint8_t*)src->pixels + y * src->pitch;
        for (int x = ix0; x < ix1; x++) {
            // compute horizontal weight: fraction of this column within [x0, x1]
            float wx = 1.0f;
            if (x < x0) wx = 1.0f - (x0 - x);    // partial left column
            if (x + 1 > x1) wx = x1 - x;          // partial right column
            if (wx <= 0) continue;

            float w = wx * wy;
            uint32_t pixel = *((uint32_t*)(row + x * 4));
            sum_r += (pixel & 0xFF) * w;
            sum_g += ((pixel >> 8) & 0xFF) * w;
            sum_b += ((pixel >> 16) & 0xFF) * w;
            sum_a += ((pixel >> 24) & 0xFF) * w;
            total_weight += w;
        }
    }

    if (total_weight <= 0) return 0;

    float inv = 1.0f / total_weight;
    uint8_t r = (uint8_t)std::min(255.0f, sum_r * inv + 0.5f);
    uint8_t g = (uint8_t)std::min(255.0f, sum_g * inv + 0.5f);
    uint8_t b = (uint8_t)std::min(255.0f, sum_b * inv + 0.5f);
    uint8_t a = (uint8_t)std::min(255.0f, sum_a * inv + 0.5f);

    return r | (g << 8) | (b << 16) | (a << 24);
}

// Enhanced blit function with support for different scaling modes
void blit_surface_scaled(ImageSurface* src, Rect* src_rect, ImageSurface* dst, Rect* dst_rect, Bound* clip, ScaleMode scale_mode,
                         ClipShape** clip_shapes, int clip_depth) {
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
    bool downscaling = (x_ratio > 1.5f || y_ratio > 1.5f); // use area averaging for significant downscales
    int left = (int)std::max(clip->left, dst_rect->x);
    int right = (int)std::min(clip->right, dst_rect->x + dst_rect->width);
    int top = (int)std::max(clip->top, dst_rect->y);
    int bottom = (int)std::min(clip->bottom, dst_rect->y + dst_rect->height);
    if (left >= right || top >= bottom) return; // dst_rect outside the dst surface

    // Check if clip shape clipping is needed
    bool need_shape_clip = (clip_depth > 0) &&
        !clip_shapes_rect_inside(clip_shapes, clip_depth,
            (float)left + 0.5f, (float)top + 0.5f,
            (float)(right - left - 1), (float)(bottom - top - 1));

    int y_off = dst->tile_offset_y;  // subtract to get tile-relative row index
    for (int i = top; i < bottom; i++) {
        int row_left = left, row_right = right;
        if (need_shape_clip) {
            float py = (float)i + 0.5f;
            clip_shapes_scanline_bounds(clip_shapes, clip_depth, py, left, right, &row_left, &row_right);
            if (row_left >= row_right) continue;
        }
        uint8_t* row_pixels = (uint8_t*)dst->pixels + (i - y_off) * dst->pitch;
        for (int j = row_left; j < row_right; j++) {
            float src_x = src_rect->x + (j - dst_rect->x) * x_ratio;
            float src_y = src_rect->y + (i - dst_rect->y) * y_ratio;

            uint8_t* dst_pixel = (uint8_t*)row_pixels + (j * 4);

            uint32_t src_color;
            if (scale_mode == SCALE_MODE_LINEAR && downscaling) {
                // area averaging: average all source pixels mapping to this destination pixel
                float box_x0 = src_rect->x + (j - dst_rect->x) * x_ratio;
                float box_y0 = src_rect->y + (i - dst_rect->y) * y_ratio;
                float box_x1 = box_x0 + x_ratio;
                float box_y1 = box_y0 + y_ratio;
                src_color = area_average(src, box_x0, box_y0, box_x1, box_y1);
            }
            else if (scale_mode == SCALE_MODE_LINEAR) {
                // Bilinear interpolation with pixel-center alignment for correct upscale
                float bx = src_rect->x + (j - dst_rect->x + 0.5f) * x_ratio - 0.5f;
                float by = src_rect->y + (i - dst_rect->y + 0.5f) * y_ratio - 0.5f;
                src_color = bilinear_interpolate(src, bx, by);
            }
            else if (scale_mode == SCALE_MODE_LINEAR_WRAP) {
                // Bilinear with wrap-around for tiled/repeating backgrounds
                float bx = src_rect->x + (j - dst_rect->x + 0.5f) * x_ratio - 0.5f;
                float by = src_rect->y + (i - dst_rect->y + 0.5f) * y_ratio - 0.5f;
                src_color = bilinear_interpolate_wrap(src, bx, by);
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
            rdt_picture_free(img_surface->pic);
        }
        if (img_surface->source_path) mem_free(img_surface->source_path);
        if (img_surface->source_data) mem_free(img_surface->source_data);
        mem_free(img_surface);
    }
}

void image_surface_ensure_decoded(ImageSurface* img) {
    if (!img || img->pixels) return;  // already decoded or null

    if (img->source_path) {
        // decode from local file
        int width, height, channels;
        unsigned char* data = image_load(img->source_path, &width, &height, &channels, 4);
        if (data) {
            img->pixels = data;
            img->pitch = width * 4;
            log_debug("[image] Decoded local image on demand: %dx%d from %s", width, height, img->source_path);
        } else {
            log_error("[image] Failed to decode local image: %s", img->source_path);
        }
        mem_free(img->source_path);
        img->source_path = NULL;
    } else if (img->source_data) {
        // decode from memory buffer
        int width, height, channels;
        unsigned char* data = image_load_from_memory(img->source_data, img->source_data_len, &width, &height, &channels);
        if (data) {
            img->pixels = data;
            img->pitch = width * 4;
            log_debug("[image] Decoded HTTP image on demand: %dx%d", width, height);
        } else {
            log_error("[image] Failed to decode HTTP image from memory");
        }
        mem_free(img->source_data);
        img->source_data = NULL;
        img->source_data_len = 0;
    }
}
