#include "view.hpp"
#include "render.hpp"
#include "radiant.hpp"
#include "event.hpp"
#include "svg_animation.hpp"

#include "../lib/image.h"
#include "../lib/log.h"
#include "../lib/hashmap_typed.hpp"
#include "../lib/memtrack.h"
#include "../lib/base64.h"
#include "../lib/url.h"
#include "../lib/file.h"
#include "../lib/path_str.h"
#include "../lib/str.h"
#include "../lib/endian.h"
#include "../lambda/input/input.hpp"  // for download_http_content
#include "../lambda/network/network_resource_manager.h"

#include <stdlib.h>
#include <limits.h>
#include <unistd.h>

typedef struct ImageEntry {
    // ImageFormat format;
    const char* path;  // todo: change to URL
    ImageSurface *image;
    bool unavailable;
} ImageEntry;

typedef TypedHashMap<ImageEntry,
    HashMapCStrMemberKeyOps<ImageEntry, &ImageEntry::path>> ImageMap;

static char* resolve_wpt_absolute_image_path(UiContext* uicon, const char* img_url) {
    if (!uicon || !uicon->document) return nullptr;
    return radiant_resolve_wpt_resource_path(img_url,
        uicon->document->url, MEM_CAT_RENDER);
}

// Detect if memory content is SVG by checking for XML/SVG signature
bool image_content_is_svg(const unsigned char* data, size_t size) {
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

typedef struct SvgImageIntrinsicMetadata {
    float width;
    float height;
    bool has_width;
    bool has_height;
    bool has_ratio;
} SvgImageIntrinsicMetadata;

static const char* svg_root_tag_end(const char* svg, const char* end) {
    if (!svg || !end) return NULL;
    const char* tag_end = strn_scan_top_level(svg, end, ">", '(', ')', "\"'", false);
    return tag_end < end ? tag_end : NULL;
}

static bool svg_find_root_attr(const char* svg, const char* tag_end, const char* name,
                               char* out, size_t out_cap) {
    if (!svg || !tag_end || !name || !out || out_cap == 0) return false;

    const char* p = svg + 4;
    size_t name_len = strlen(name);
    while (p < tag_end) {
        while (p < tag_end && isspace((unsigned char)*p)) p++;
        if (p >= tag_end || *p == '/' || *p == '>') break;

        const char* attr_start = p;
        while (p < tag_end &&
               (isalnum((unsigned char)*p) || *p == ':' || *p == '_' || *p == '-')) {
            p++;
        }
        size_t attr_len = (size_t)(p - attr_start);
        while (p < tag_end && isspace((unsigned char)*p)) p++;
        if (p >= tag_end || *p != '=') {
            while (p < tag_end && !isspace((unsigned char)*p)) p++;
            continue;
        }
        p++;
        while (p < tag_end && isspace((unsigned char)*p)) p++;
        if (p >= tag_end) break;

        char quote = 0;
        if (*p == '"' || *p == '\'') {
            quote = *p;
            p++;
        }
        const char* value_start = p;
        if (quote) {
            while (p < tag_end && *p != quote) p++;
        } else {
            while (p < tag_end && !isspace((unsigned char)*p) && *p != '>') p++;
        }
        const char* value_end = p;
        if (quote && p < tag_end) p++;

        if (attr_len == name_len && strncmp(attr_start, name, name_len) == 0) {
            size_t value_len = (size_t)(value_end - value_start);
            if (value_len >= out_cap) value_len = out_cap - 1;
            str_copy(out, out_cap, value_start, value_len);
            return true;
        }
    }

    return false;
}

static bool svg_parse_viewbox_attr(const char* value, float* width, float* height) {
    if (!value || !width || !height) return false;

    float values[4];
    if (str_parse_float_list(value, ", \t\n\r\f\v", values, 4, NULL) != 4) return false;
    float vb_width = values[2];
    float vb_height = values[3];
    if (vb_width <= 0.0f || vb_height <= 0.0f) return false;

    *width = vb_width;
    *height = vb_height;
    return true;
}

static bool svg_parse_definite_length_attr(const char* value, float* out_value) {
    if (!value || !out_value) return false;

    const char* p = value;
    p = str_skip_ascii_space(p);
    char* parsed_end = NULL;
    float length = strtof(p, &parsed_end);
    if (parsed_end == p || length <= 0.0f) return false;

    const char* end_ptr = str_skip_ascii_space(parsed_end);
    if (*end_ptr == '%') return false;
    if (strncmp(end_ptr, "px", 2) == 0 || *end_ptr == '\0') {
        *out_value = length;
        return true;
    }
    if (strncmp(end_ptr, "in", 2) == 0) {
        *out_value = length * 96.0f;
        return true;
    }
    if (strncmp(end_ptr, "cm", 2) == 0) {
        *out_value = length * (96.0f / 2.54f);
        return true;
    }
    if (strncmp(end_ptr, "mm", 2) == 0) {
        *out_value = length * (96.0f / 25.4f);
        return true;
    }
    if (strncmp(end_ptr, "pt", 2) == 0) {
        *out_value = length * (96.0f / 72.0f);
        return true;
    }
    if (strncmp(end_ptr, "pc", 2) == 0) {
        *out_value = length * 16.0f;
        return true;
    }

    return false;
}

static SvgImageIntrinsicMetadata svg_read_intrinsic_metadata_in_memory(const char* data, size_t size) {
    SvgImageIntrinsicMetadata meta = {0.0f, 0.0f, false, false, false};
    if (!data || size == 0) return meta;

    const char* end = data + size;
    size_t svg_offset = str_find(data, size, "<svg", 4);
    const char* svg = svg_offset == STR_NPOS ? NULL : data + svg_offset;
    if (!svg) return meta;

    const char* tag_end = svg_root_tag_end(svg, end);
    if (!tag_end) return meta;

    char width_attr[128];
    char height_attr[128];
    char viewbox_attr[160];
    bool has_width_attr = svg_find_root_attr(svg, tag_end, "width", width_attr, sizeof(width_attr));
    bool has_height_attr = svg_find_root_attr(svg, tag_end, "height", height_attr, sizeof(height_attr));
    bool has_viewbox_attr = svg_find_root_attr(svg, tag_end, "viewBox", viewbox_attr, sizeof(viewbox_attr));
    if (!has_viewbox_attr) {
        has_viewbox_attr = svg_find_root_attr(svg, tag_end, "viewbox", viewbox_attr, sizeof(viewbox_attr));
    }

    float viewbox_width = 0.0f;
    float viewbox_height = 0.0f;
    if (has_viewbox_attr && svg_parse_viewbox_attr(viewbox_attr, &viewbox_width, &viewbox_height)) {
        meta.has_ratio = true;
    }

    if (has_width_attr && svg_parse_definite_length_attr(width_attr, &meta.width)) {
        meta.has_width = true;
    }
    if (has_height_attr && svg_parse_definite_length_attr(height_attr, &meta.height)) {
        meta.has_height = true;
    }

    if (meta.has_width && !meta.has_height && meta.has_ratio) {
        meta.height = meta.width * viewbox_height / viewbox_width;
        meta.has_height = true;
    } else if (!meta.has_width && meta.has_height && meta.has_ratio) {
        meta.width = meta.height * viewbox_width / viewbox_height;
        meta.has_width = true;
    } else if (!meta.has_width && !meta.has_height && meta.has_ratio) {
        meta.width = viewbox_width;
        meta.height = viewbox_height;
    }

    return meta;
}

static SvgImageIntrinsicMetadata svg_read_intrinsic_metadata_in_file(const char* file_path) {
    SvgImageIntrinsicMetadata meta = {0.0f, 0.0f, false, false, false};
    if (!file_path) return meta;

    FILE* fp = fopen(file_path, "rb");
    if (!fp) return meta;

    char buffer[4096];
    size_t read_count = fread(buffer, 1, sizeof(buffer), fp);
    fclose(fp);
    return svg_read_intrinsic_metadata_in_memory(buffer, read_count);
}

static int svg_dimension_to_image_px(float value) {
    if (value <= 0.0f) return 0;
    // INT_CAST_OK: ImageSurface dimensions are integer CSS pixels.
    return value < 1.0f ? 1 : (int)(value + 0.5f);
}

static void image_surface_apply_svg_metadata(ImageSurface* surface,
                                             SvgImageIntrinsicMetadata meta,
                                             float fallback_width,
                                             float fallback_height) {
    if (!surface) return;

    if (meta.width <= 0.0f && fallback_width > 0.0f) meta.width = fallback_width;
    if (meta.height <= 0.0f && fallback_height > 0.0f) meta.height = fallback_height;
    if (meta.width <= 0.0f) meta.width = 300.0f;
    if (meta.height <= 0.0f) {
        meta.height = meta.has_ratio && meta.width > 0.0f ? meta.width * 0.5f : 150.0f;
    }

    surface->width = svg_dimension_to_image_px(meta.width);
    surface->height = svg_dimension_to_image_px(meta.height);
    surface->encoded_width = surface->width;
    surface->encoded_height = surface->height;
    surface->orientation = 1;
    surface->has_intrinsic_size = meta.has_width && meta.has_height;
    surface->has_intrinsic_aspect_ratio = meta.has_ratio;
    surface->generation = 1;
}

static uint16_t read_exif_u16(const unsigned char* p, bool little_endian) {
    return little_endian ? read_le16((const uint8_t*)p) : read_be16((const uint8_t*)p);
}

static uint32_t read_exif_u32(const unsigned char* p, bool little_endian) {
    return little_endian ? read_le32((const uint8_t*)p) : read_be32((const uint8_t*)p);
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

    lam::Temp<unsigned char> bytes = lam::temp_array<unsigned char>((size_t)file_size, MEM_CAT_IMAGE);
    if (!bytes) {
        fclose(fp);
        return 1;
    }
    size_t read_count = fread(bytes.get(), 1, (size_t)file_size, fp);
    fclose(fp);

    int orientation = 1;
    if (read_count == (size_t)file_size) {
        orientation = jpeg_exif_orientation_from_memory(bytes.get(), (size_t)file_size);
    }
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

// Takes the path; the image cache keeps it as the key of an unavailable entry.
static void image_cache_store_unavailable(UiContext* uicon, lam::Temp<char> file_path) {
    if (!file_path || !uicon || !uicon->image_cache) return;
    // Cache the failed URL for this document so repeated intrinsic-size probes
    // do not retry a synchronous network fetch that already failed.
    ImageEntry entry = {.path = file_path.release(), .image = nullptr, .unavailable = true};
    ImageMap::set(uicon->image_cache, entry);
}

static void load_image_cleanup_failed(UiContext* uicon, Url* abs_url, lam::Temp<char>& file_path) {
    if (abs_url) url_destroy(abs_url);
    image_cache_store_unavailable(uicon, lam::Temp<char>(file_path.release()));
}

ImageSurface* image_surface_decode_file(const char* path) {
    if (!path || !*path) return nullptr;
    int width = 0, height = 0, channels = 0;
    unsigned char* pixels = image_load(path, &width, &height, &channels, 4);
    if (!pixels) return nullptr;
    ImageSurface* surface = image_surface_create_from(width, height, pixels);
    if (!surface) image_free(pixels);
    return surface;
}

ImageSurface* image_surface_decode_data(const unsigned char* data, size_t length) {
    int width = 0, height = 0, channels = 0;
    unsigned char* pixels = image_load_from_memory(data, length, &width, &height, &channels);
    if (!pixels) return nullptr;
    ImageSurface* surface = image_surface_create_from(width, height, pixels);
    if (!surface) image_free(pixels);
    return surface;
}

static bool image_path_has_declared_non_svg_extension(const char* file_path) {
    if (!file_path) return false;
    const char* dot = file_path_ext(file_path);
    if (!dot) return false;
    // cached network resources keep a synthetic suffix, so sniff their bytes for SVG.
    if (str_icmp_cstr(dot, ".cache") == 0) return false;
    if (str_icmp_cstr(dot, ".svg") == 0 || str_icmp_cstr(dot, ".svgz") == 0) return false;
    return true;
}

static void image_register_gif_animation(UiContext* ui, ImageSurface* image, GifFrames* frames) {
    if (!frames) return;
    DocState* state = ui && ui->document ? (DocState*)ui->document->state : nullptr;
    if (!state || !state->animation_scheduler ||
        !gif_animation_create(state->animation_scheduler, image, frames,
            state->animation_scheduler->current_time, ui->document->document_pool)) {
        image_gif_free(frames);
    }
}

static bool image_cache_hash_resource(const void* item, void* context) {
    const ImageEntry* entry = (const ImageEntry*)item;
    uint64_t* hash = (uint64_t*)context;
    uint64_t pointer = (uint64_t)(uintptr_t)entry->image;
    uint64_t generation = entry->image ? entry->image->generation : 0;
    *hash ^= (pointer * UINT64_C(0x9e3779b97f4a7c15)) ^
        (generation * UINT64_C(0xbf58476d1ce4e5b9));
    return true;
}

uint64_t image_cache_resource_generation(UiContext* ui) {
    uint64_t hash = 0;
    if (!ui) return hash;
    if (ui->image_cache) hashmap_scan(ui->image_cache, image_cache_hash_resource, &hash);
    if (ui->document && ui->document->resource_manager) {
        int total = 0, completed = 0, failed = 0;
        resource_manager_get_stats(ui->document->resource_manager, &total, &completed, &failed);
        hash ^= (uint64_t)total * UINT64_C(0x94d049bb133111eb) ^
            (uint64_t)completed * UINT64_C(0x2545f4914f6cdd1d) ^ (uint64_t)failed;
    }
    return hash;
}

ImageSurface* image_cache_adopt(UiContext* uicon, const char* key, ImageSurface* surface) {
    if (!surface) return nullptr;
    if (!uicon || !key) { image_surface_destroy(surface); return nullptr; }
    if (!uicon->image_cache) uicon->image_cache = lam::own(ImageMap::create(10));
    ImageEntry search_key = {.path = (char*)key, .image = NULL};
    ImageEntry* entry = uicon->image_cache ? ImageMap::get(uicon->image_cache, search_key) : nullptr;
    if (entry && entry->image) {
        if (entry->image != surface) image_surface_destroy(surface);
        return entry->image;
    }
    if (entry) {
        // an earlier failed probe of this file left an unavailable entry
        entry->image = surface; entry->unavailable = false;  // RETAINED_FIELD_OK: image-cache entry, not a retained DOM field
        return surface;
    }
    lam::Temp<char> path(mem_strdup(key, MEM_CAT_RENDER));
    if (!path || !uicon->image_cache) { image_surface_destroy(surface); return nullptr; }
    ImageEntry new_entry = {.path = path.release(), .image = surface};
    ImageMap::set(uicon->image_cache, new_entry);
    return surface;
}

ImageSurface* load_image(UiContext* uicon, const char *img_url) {
    if (!uicon || !img_url || !uicon->document) return nullptr;
    bool data_uri = strncmp(img_url, "data:", 5) == 0;
    // SVG2 §2.2: image documents can embed data resources but cannot fetch external content.
    if (uicon->document->services.svg_image_document && !data_uri) return nullptr;
    if (!data_uri && uicon->document->url == NULL) {
        log_error("Missing URL context for image: %s", img_url);
        return NULL;
    }

    if (uicon->image_cache == NULL) {
        // create a new hash map. 2nd argument is the initial capacity.
        // 3rd and 4th arguments are optional seeds that are passed to the following hash function.
        uicon->image_cache = lam::own(ImageMap::create(10));
    }

    // Handle data: URIs
    if (data_uri) {
        ImageEntry search_key = {.path = (char*)img_url, .image = NULL};
        ImageEntry* entry = ImageMap::get(uicon->image_cache, search_key);
        if (entry) {
            log_debug(entry->unavailable ? "[BG-IMAGE] Data URI image unavailable from cache"
                                         : "[BG-IMAGE] Data URI image loaded from cache");
            return entry->image;
        }

        const char* comma = strchr(img_url, ',');
        if (!comma) {
            log_warn("image: invalid data URI placeholder (no comma)");
            return NULL;
        }

        // Check if data URI is base64-encoded: "data:...;base64,..."
        bool is_base64 = false;
        const char* meta = img_url + 5;  // after "data:"
        size_t meta_len = comma - meta;
        for (size_t i = 0; i + 5 < meta_len; i++) {
            if (str_istarts_with(meta + i, meta_len - i, "base64", 6)) {
                is_base64 = true;
                break;
            }
        }

        size_t decoded_len = 0;
        lam::Temp<uint8_t> decoded;
        if (is_base64) {
            decoded.reset(base64_decode(comma + 1, 0, &decoded_len));
        } else {
            // URL-encoded (percent-encoded) data URI
            const char* data_str = comma + 1;
            size_t data_str_len = strlen(data_str);
            decoded.reset((uint8_t*)url_decode_component(data_str, data_str_len, &decoded_len));
            if (!decoded) {
                // WPT and browser data URIs may contain literal '%' characters;
                // keep the payload path available without weakening URL decoding.
                decoded.reset(parse_data_uri(img_url, nullptr, 0, &decoded_len));
            }
        }
        if (!decoded || decoded_len == 0) {
            log_warn("image: data URI payload unavailable, using placeholder");
            return NULL;
        }
        // Detect format from MIME type or content
        bool is_svg = image_content_is_svg(decoded.get(), decoded_len);
        ImageSurface* surface;
        GifFrames* inline_frames = nullptr;
        if (is_svg) {
            SvgImageIntrinsicMetadata svg_meta =
                svg_read_intrinsic_metadata_in_memory((const char*)decoded.get(), decoded_len);
            surface = image_surface_alloc();
            surface->format = IMAGE_FORMAT_SVG;
            surface->pic = lam::counted(rdt_picture_load_data((const char*)decoded.get(), (int)decoded_len, "svg"));
            if (!surface->pic) {
                image_surface_destroy(surface);
                return NULL;
            }
            float svg_w, svg_h;
            rdt_picture_get_size(surface->pic, &svg_w, &svg_h);
            image_surface_apply_svg_metadata(surface, svg_meta, svg_w, svg_h);
        } else {
            int width, height;
            int orientation = jpeg_exif_orientation_from_memory(decoded.get(), decoded_len);
            if (!image_get_dimensions_from_memory(decoded.get(), decoded_len, &width, &height)) {
                // Invalid inline payloads are common in scraped pages; probe
                // before full decode so placeholders do not emit backend errors.
                log_warn("image: unsupported data URI image, using placeholder");
                return NULL;
            }
            surface = image_surface_decode_data(decoded.get(), decoded_len);
            if (surface) inline_frames = gif_detect_animated_from_memory(decoded.get(), decoded_len);
            decoded.reset();
            if (!surface) {
                log_warn("image: unsupported data URI image, using placeholder");
                return NULL;
            }
            // Detect format from MIME
            if (strstr(img_url, "image/png")) surface->format = IMAGE_FORMAT_PNG;
            else if (strstr(img_url, "image/jpeg") || strstr(img_url, "image/jpg")) surface->format = IMAGE_FORMAT_JPEG;
            else if (strstr(img_url, "image/gif")) surface->format = IMAGE_FORMAT_GIF;
            else if (strstr(img_url, "image/webp")) surface->format = IMAGE_FORMAT_WEBP;
            else if (strstr(img_url, "image/svg")) surface->format = IMAGE_FORMAT_SVG;
            if (surface->format == IMAGE_FORMAT_JPEG) {
                image_surface_apply_orientation_metadata(surface, orientation);
            } else {
                image_surface_apply_orientation_metadata(surface, 1);
            }
        }
        image_register_gif_animation(uicon, surface, inline_frames);
        char* cache_path = mem_strdup(img_url, MEM_CAT_RENDER);
        if (!cache_path) {
            image_surface_destroy(surface);
            return NULL;
        }
        // Inline image surfaces used to escape the shared cache, so shutdown
        // had no owner to release their decoded pixels.
        ImageEntry new_entry = {.path = (char*)cache_path, .image = surface};
        ImageMap::set(uicon->image_cache, new_entry);
        svg_image_animation_register(uicon, surface);
        log_debug("[BG-IMAGE] Loaded data URI image: %dx%d", surface->width, surface->height);
        return surface;
    }
    const char* resolved_img_url = img_url;
    lam::Temp<char> local_file_url;
    if (file_exists(img_url)) {
        char abs_path[4096];
#ifdef _WIN32
        bool is_absolute_path = path_str_win32_is_absolute(img_url);
#else
        bool is_absolute_path = path_str_posix_is_absolute(img_url);
#endif
        if (is_absolute_path) {
            // a native absolute path must not acquire a second working-directory prefix.
            str_copy(abs_path, sizeof(abs_path), img_url, strlen(img_url));
        } else {
            char cwd_buf[4096];
            if (getcwd(cwd_buf, sizeof(cwd_buf))) {
                str_fmt(abs_path, sizeof(abs_path), "%s/%s", cwd_buf, img_url);
            } else {
                abs_path[0] = '\0';
            }
        }
        if (abs_path[0]) {
            // Cached network resources are local files even when the document
            // base URL is HTTP, so resolve them as file URLs.
            local_file_url.reset(url_from_local_path(abs_path));
            if (local_file_url) resolved_img_url = local_file_url.get();
        }
    }

    Url* abs_url = parse_url(uicon->document->url, resolved_img_url);
    local_file_url.reset();
    if (!abs_url) {
        log_error("Failed to parse URL: %s", img_url);
        return NULL;
    }

    // Check if this is an HTTP URL
    bool is_http = (abs_url->scheme == URL_SCHEME_HTTP || abs_url->scheme == URL_SCHEME_HTTPS);
    lam::Temp<char> file_path;
    lam::Temp<unsigned char> downloaded_data;
    size_t downloaded_size = 0;

    if (is_http) {
        // Network-managed documents cannot let layout/render perform blocking
        // HTTP fetches. Loader-stage CSS prefetches may already have completed;
        // consume those bytes without admitting a new synchronous transfer.
        if (uicon->document->resource_manager) {
            const char* url_str = url_get_href(abs_url);
            downloaded_data.reset((unsigned char*)resource_manager_copy_ready_resource_content(
                uicon->document->resource_manager, url_str, &downloaded_size));
            if (!downloaded_data || downloaded_size == 0) {
                log_debug("[image] Network image not ready without blocking: %s", url_str);
                url_destroy(abs_url);
                return NULL;
            }
            file_path.reset(mem_strdup(url_str, MEM_CAT_RENDER));
            if (!file_path) {
                url_destroy(abs_url);
                return NULL;
            }
            log_debug("[image] Consumed ready network image bytes: %s", url_str);
        } else {
            // Download the image from HTTP URL
            const char* url_str = url_get_href(abs_url);
            file_path.reset(mem_strdup(url_str, MEM_CAT_RENDER));
            if (!file_path) {
                url_destroy(abs_url);
                return NULL;
            }
            ImageEntry search_key = {.path = file_path.get(), .image = NULL};
            ImageEntry* entry = ImageMap::get(uicon->image_cache, search_key);
            if (entry) {
                log_debug(entry->unavailable ? "Image unavailable from cache: %s"
                                             : "Image loaded from cache: %s", file_path.get());
                url_destroy(abs_url);
                return entry->image;
            }
            log_debug("[image] Downloading image from URL: %s", url_str);
            downloaded_data.reset((unsigned char*)download_http_content(url_str, &downloaded_size, nullptr));
            if (!downloaded_data || downloaded_size == 0) {
                log_error("[image] Failed to download image: %s", url_str);
                load_image_cleanup_failed(uicon, abs_url, file_path);
                return NULL;
            }
            log_debug("[image] Downloaded image: %zu bytes", downloaded_size);
        }
    } else {
        file_path.reset(url_to_local_path(abs_url));
        if (!file_path) {
            log_error("Invalid local URL: %s", img_url);
            url_destroy(abs_url);
            return NULL;
        }
        if (!file_exists(file_path.get())) {
            char shared_path[4096];
            const char* doc_href = uicon->document ? url_get_href(uicon->document->url) : nullptr;
            if (radiant_resolve_shared_data_resource_path(img_url, doc_href,
                                                          shared_path, sizeof(shared_path))) {
                char* shared_copy = mem_strdup(shared_path, MEM_CAT_RENDER);
                if (shared_copy) file_path.reset(shared_copy);
            }
        }
        if (img_url[0] == '/' && img_url[1] != '/' && !file_exists(file_path.get())) {
            // Local WPT runs emulate an HTTP server; URL-absolute resources are
            // rooted at the WPT tree, not at the host filesystem root.
            char* wpt_path = resolve_wpt_absolute_image_path(uicon, img_url);
            if (wpt_path) {
                log_debug("[image] Resolved WPT absolute resource %s -> %s", img_url, wpt_path);
                file_path.reset(wpt_path);
            }
        }
    }

    ImageEntry search_key = {.path = file_path.get(), .image = NULL};
    ImageEntry* entry = ImageMap::get(uicon->image_cache, search_key);
    if (entry) {
        log_debug(entry->unavailable ? "Image unavailable from cache: %s"
                                     : "Image loaded from cache: %s", file_path.get());
        url_destroy(abs_url);
        return entry->image;
    }
    else {
        log_debug("Image not found in cache: %s", file_path.get());
    }

    ImageSurface *surface;
    int slen = strlen(file_path.get());
    // load image data
    log_debug("loading image at: %s", file_path.get());

    // Determine if this is an SVG - check content for HTTP, extension for local files
    bool is_svg = false;
    if (is_http && downloaded_data) {
        is_svg = image_content_is_svg(downloaded_data.get(), downloaded_size);
        log_debug("[image] HTTP image format detection: is_svg=%s", is_svg ? "yes" : "no");
    } else {
        is_svg = (slen > 4 && strcmp(file_path.get() + slen - 4, ".svg") == 0);
        if (!is_svg && !image_path_has_declared_non_svg_extension(file_path.get())) {
            FILE* svg_probe = fopen(file_path.get(), "rb");
            if (svg_probe) {
                unsigned char probe_buf[512];
                size_t probe_size = fread(probe_buf, 1, sizeof(probe_buf), svg_probe);
                fclose(svg_probe);
                // Network cache files do not preserve extensions; declared
                // raster resources keep their URL-selected decoder.
                is_svg = image_content_is_svg(probe_buf, probe_size);
            }
        }
    }

    if (is_svg) {
        SvgImageIntrinsicMetadata svg_meta = is_http && downloaded_data
            ? svg_read_intrinsic_metadata_in_memory((const char*)downloaded_data.get(), downloaded_size)
            : svg_read_intrinsic_metadata_in_file(file_path.get());
        surface = image_surface_alloc();
        surface->format = IMAGE_FORMAT_SVG;
        if (is_http && downloaded_data) {
            surface->pic = lam::counted(rdt_picture_load_data((const char*)downloaded_data.get(), (int)downloaded_size, "svg"));
        } else {
            surface->pic = lam::counted(rdt_picture_load(file_path.get()));
        }
        if (!surface->pic) {
            log_debug("failed to load SVG image: %s", file_path.get());
            image_surface_destroy(surface);
            load_image_cleanup_failed(uicon, abs_url, file_path);
            return NULL;
        }
        float svg_w, svg_h;
        rdt_picture_get_size(surface->pic, &svg_w, &svg_h);
        image_surface_apply_svg_metadata(surface, svg_meta, svg_w, svg_h);
        log_debug("SVG image size: %d x %d (picture %.1f x %.1f, intrinsic=%d)",
                  surface->width, surface->height, svg_w, svg_h, surface->has_intrinsic_size);
    }
    // Detect Lottie JSON animation (by extension for local, by content for HTTP)
    else if ((!is_http && lottie_detect_by_path(file_path.get())) ||
             (is_http && downloaded_data && lottie_detect_by_content(downloaded_data.get(), downloaded_size))) {
        // Create a placeholder surface — pixels will be filled by the LottiePlayer
        surface = image_surface_alloc();
        // Try to get natural dimensions from the Lottie via ThorVG picture
        // For now, use a default render size; the layout will resize as needed
        surface->width = 300;   // default Lottie render width
        surface->height = 300;  // default Lottie render height
        surface->format = IMAGE_FORMAT_UNKNOWN;  // Lottie player supplies raster pixels

        // Register with animation scheduler if available
        if (uicon->document && uicon->document->state) {
            DocState* rs = (DocState*)uicon->document->state;
            if (rs && rs->animation_scheduler) {
                AnimationInstance* inst = NULL;
                if (is_http && downloaded_data) {
                    inst = lottie_player_create_from_data(rs->animation_scheduler, surface,
                                (const char*)downloaded_data.get(), downloaded_size,
                                surface->width, surface->height,
                                rs->animation_scheduler->current_time, uicon->document->document_pool);
                } else {
                    inst = lottie_player_create_from_file(rs->animation_scheduler, surface,
                                file_path.get(), surface->width, surface->height,
                                rs->animation_scheduler->current_time, uicon->document->document_pool);
                }
                if (inst) {
                    LottiePlayer* lp = (LottiePlayer*)inst->state;
                    if (lp) {
                        surface->width = lp->width;
                        surface->height = lp->height;
                    }
                    log_info("lottie animated: registered with scheduler from %s", file_path.get());
                } else {
                    // Not a valid Lottie — fall through to raster path is not possible here
                    // Free and return NULL
                    log_debug("lottie detect: failed to load as Lottie: %s", file_path.get());
                    image_surface_destroy(surface);
                    surface = NULL;
                }
            }
        }
        downloaded_data.reset();
        if (!surface) {
            load_image_cleanup_failed(uicon, abs_url, file_path);
            return NULL;
        }
        image_surface_apply_orientation_metadata(surface, 1);
    }
    else {
        int width, height;
        if (is_http && downloaded_data) {
            // HTTP images: read dimensions from memory header, keep data for lazy decode
            if (image_get_dimensions_from_memory(downloaded_data.get(), downloaded_size, &width, &height)) {
                surface = image_surface_alloc();
                surface->width = width;
                surface->height = height;
                {
                    lam::SessionPtr<unsigned char> source_data(downloaded_data.release());
                    radiant_take_image_source_data(surface, source_data, downloaded_size);
                }
                // pixels stays NULL — decoded on demand
                log_debug("[image] Lazy load HTTP image: %dx%d (%zu bytes)", width, height, downloaded_size);
            } else {
                // Unsupported HTTP assets must enter the cached placeholder path
                // instead of sending their opaque bytes to the raster decoder.
                log_warn("image: unsupported HTTP image, using placeholder: %s", file_path.get());
                downloaded_data.reset();
                load_image_cleanup_failed(uicon, abs_url, file_path);
                return NULL;
            }
        } else {
            // Local files: read dimensions from file header only
            if (image_get_dimensions(file_path.get(), &width, &height)) {
                surface = image_surface_alloc();
                surface->width = width;
                surface->height = height;
                {
                    lam::SessionPtr<char> source_path = lam::session_strdup(file_path.get(), MEM_CAT_IMAGE);
                    radiant_take_image_source_path(surface, source_path);
                }
                // pixels stays NULL — decoded on demand
                log_debug("[image] Lazy load local image: %dx%d from %s", width, height, file_path.get());
            } else {
                // Fallback: full decode if header read fails
                surface = image_surface_decode_file(file_path.get());
                if (!surface) {
                    log_debug("failed to load image: %s", file_path.get());
                    load_image_cleanup_failed(uicon, abs_url, file_path);
                    return NULL;
                }
            }
        }
        if (slen > 5 && strcmp(file_path.get() + slen - 5, ".jpeg") == 0) {
            surface->format = IMAGE_FORMAT_JPEG;
        }
        else if (slen > 4 && strcmp(file_path.get() + slen - 4, ".jpg") == 0) {
            surface->format = IMAGE_FORMAT_JPEG;
        }
        else if (slen > 4 && strcmp(file_path.get() + slen - 4, ".png") == 0) {
            surface->format = IMAGE_FORMAT_PNG;
        }
        else if (slen > 4 && strcmp(file_path.get() + slen - 4, ".gif") == 0) {
            surface->format = IMAGE_FORMAT_GIF;
        }
        else if (slen > 5 && str_icmp_cstr(file_path.get() + slen - 5, ".webp") == 0) {
            surface->format = IMAGE_FORMAT_WEBP;
        }
        if (surface->format == IMAGE_FORMAT_JPEG) {
            int orientation = 1;
            if (is_http && surface->source_data && surface->source_data_len > 0) {
                orientation = jpeg_exif_orientation_from_memory(surface->source_data, surface->source_data_len);
            } else {
                orientation = jpeg_exif_orientation_from_file(file_path.get());
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
        image_register_gif_animation(uicon, surface, gif_frames);
    }

    ImageEntry new_entry = {.path = file_path.release(), .image = surface};
    ImageMap::set(uicon->image_cache, new_entry);
    svg_image_animation_register(uicon, surface);
    return surface;
}

bool image_entry_free(const void *item, void *udata) {
    UiContext* ui = (UiContext*)udata;
    ImageEntry* entry = (ImageEntry*)item;
    // the cache owns its key: mem_strdup for HTTP paths, url_to_local_path for local paths
    lam::Temp<char> path((char*)entry->path);
    if (entry->image) {
        // cached media surfaces can be released before the document's scheduler is torn down.
        if (ui && ui->document && ui->document->state)
            animation_scheduler_remove_by_target(ui->document->state->animation_scheduler, entry->image);
        if (entry->image->url) url_destroy(entry->image->url);
        image_surface_destroy(entry->image);
    }
    return true;
}

void image_cache_cleanup(UiContext* uicon) {
    // loop through the hashmap and free the images
    if (uicon->image_cache) {
        log_debug("Cleaning up cached images");
        hashmap_scan(uicon->image_cache, image_entry_free, uicon);
        ImageMap::destroy(uicon->image_cache);
        uicon->image_cache = NULL;
    }
    // surfaces destroyed during a render session were queued; release them now
    image_surface_drain_retired();
}

ImageSurface* image_surface_create(int pixel_width, int pixel_height) {
    if (pixel_width <= 0 || pixel_height <= 0 || pixel_width > INT_MAX / 4 ||
        (size_t)pixel_height > SIZE_MAX / sizeof(uint32_t) / (size_t)pixel_width) {
        log_error("[surface] Invalid image surface dimensions");
        return NULL;
    }
    ImageSurface* img_surface = image_surface_alloc();
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
    // pitch counts bytes; the allocation counts pixels once and checks products before multiplying.
    image_surface_adopt_pixels(img_surface, mem_calloc((size_t)pixel_width * (size_t)pixel_height, sizeof(uint32_t), MEM_CAT_IMAGE));
    if (!img_surface->pixels) {
        log_error("[surface] Could not allocate memory for image surface");
        image_surface_destroy(img_surface);
        return NULL;
    }
    return img_surface;
}

bool render_surface_allocation_size(float width, float height, size_t* bytes) {
    width = ceilf(width); height = ceilf(height);
    if (!bytes || !isfinite(width) || !isfinite(height) || width <= 0.0f || height <= 0.0f ||
        width >= (float)(INT_MAX / 4) || height >= (float)INT_MAX ||
        (double)width * (double)height > (double)((SIZE_MAX - sizeof(ImageSurface)) / 4)) return false;
    *bytes = (size_t)width * (size_t)height * 4 + sizeof(ImageSurface);
    return true;
}

ImageSurface* render_surface_create_budgeted(MemContext* memory, float width, float height) {
    size_t bytes = 0;
    if (!render_surface_allocation_size(width, height, &bytes)) return nullptr;
    width = ceilf(width); height = ceilf(height);
    if (!render_memory_allow_allocation(memory, bytes)) return nullptr;
    ImageSurface* surface = image_surface_create((int)width, (int)height); // INT_CAST_OK: checked physical raster extent
    if (!surface) {
        // reclaim outside allocator callbacks; active filter programs stay pinned while retrying their surfaces.
        mem_context_request_reclaim(memory, MEM_PRESSURE_CRITICAL, bytes);
        if (render_memory_allow_allocation(memory, bytes)) surface = image_surface_create((int)width, (int)height); // INT_CAST_OK: checked physical raster extent
    }
    return surface;
}

ImageSurface* image_surface_create_from(int pixel_width, int pixel_height, void* pixels) {
    if (pixel_width <= 0 || pixel_height <= 0 || pixel_width > INT_MAX / 4 || !pixels ||
        (size_t)pixel_height > SIZE_MAX / sizeof(uint32_t) / (size_t)pixel_width) {
        log_error("[surface] Invalid image surface dimensions or pixels");
        return NULL;
    }
    ImageSurface* img_surface = image_surface_alloc();
    if (img_surface) {
        img_surface->width = pixel_width;  img_surface->height = pixel_height;
        img_surface->encoded_width = pixel_width;  img_surface->encoded_height = pixel_height;
        img_surface->orientation = 1;
        img_surface->has_intrinsic_size = true;
        img_surface->pitch = pixel_width * 4;
        image_surface_adopt_pixels(img_surface, pixels);  // the surface takes the decoded buffer
        img_surface->alpha_mode = IMAGE_ALPHA_STRAIGHT;
        img_surface->generation = 1;
    }
    return img_surface;
}

void fill_surface_rect(ImageSurface* surface, Rect* rect, uint32_t color, Bound* clip,
                       ClipShape** clip_shapes, int clip_depth) {
    RasterPaintContext ctx = raster_paint_context(surface, clip, clip_shapes, clip_depth);
    raster_fill_rect(&ctx, rect, color);
}

// Enhanced blit function with support for different scaling modes
void blit_surface_scaled(ImageSurface* src, Rect* src_rect, ImageSurface* dst, Rect* dst_rect, Bound* clip, ScaleMode scale_mode,
                         ClipShape** clip_shapes, int clip_depth) {
    RasterPaintContext ctx = raster_paint_context(dst, clip, clip_shapes, clip_depth);
    raster_blit_surface_scaled(&ctx, src, src_rect, dst_rect, scale_mode, 255);
}



void image_surface_destroy(ImageSurface* img_surface) {
    if (!img_surface) return;
    // stale handles stop resolving at once; the storage goes at the quiet point
    // when a render session may still be reading it (image_surface_generation.cpp)
    if (image_surface_defer_release(img_surface)) return;
    image_surface_release_now(img_surface);
}

void image_surface_adopt_pixels(ImageSurface* img_surface, void* pixels) {
    if (!img_surface) return;
    lam::free_owned(img_surface->owned_pixels);
    img_surface->owned_pixels = lam::own_arr((uint8_t*)pixels);
    img_surface->pixels = pixels;
}

static bool image_surface_can_promote_decode(ImageSurface* img, int target_w, int target_h) {
    if (!img || !img->pixels) return true;
    if (img->format != IMAGE_FORMAT_PNG && img->format != IMAGE_FORMAT_JPEG) return false;
    if (!img->source_path && !img->source_data) return false;
    int decoded_w = img->decoded_width > 0 ? img->decoded_width : img->width;
    int decoded_h = img->decoded_height > 0 ? img->decoded_height : img->height;
    if (target_w <= 0) target_w = decoded_w;
    if (target_h <= 0) target_h = decoded_h;
    // decoding cannot exceed the encoded image; larger CSS boxes must not
    // repeatedly replace full-resolution buffers already borrowed by paint.
    int intrinsic_w = img->encoded_width > 0 ? img->encoded_width : img->width;
    int intrinsic_h = img->encoded_height > 0 ? img->encoded_height : img->height;
    if (intrinsic_w > 0 && target_w > intrinsic_w) target_w = intrinsic_w;
    if (intrinsic_h > 0 && target_h > intrinsic_h) target_h = intrinsic_h;
    return target_w > decoded_w || target_h > decoded_h;
}

static bool image_decode_trace_enabled(void) {
    const char* trace = getenv("LAMBDA_IMAGE_DECODE_TRACE");
    return trace && trace[0] != '\0';
}

static void image_surface_install_decoded_pixels(ImageSurface* img,
                                                 unsigned char* pixels,
                                                 int width, int height) {
    image_surface_adopt_pixels(img, pixels);
    img->alpha_mode = IMAGE_ALPHA_STRAIGHT;
    img->decoded_width = width;
    img->decoded_height = height;
    img->pitch = width * 4;
    image_surface_bump_generation(img);
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
            // record actual decoded buffer dims; intrinsic width/height stay unchanged for layout.
            image_surface_install_decoded_pixels(img, data, width, height);
            // Release builds strip debug logs; keep this opt-in trace for
            // cache-promotion tests without raising normal image decodes to note.
            if (image_decode_trace_enabled()) {
                log_notice("[image] Decoded local image on demand: %dx%d (intrinsic %dx%d, target %dx%d) from %s",
                           width, height, img->width, img->height, target_w, target_h, img->source_path);
            } else {
                log_debug("[image] Decoded local image on demand: %dx%d (intrinsic %dx%d, target %dx%d) from %s",
                          width, height, img->width, img->height, target_w, target_h, img->source_path);
            }
        } else {
            // Lazy decode happens after layout chose an intrinsic placeholder;
            // unsupported/corrupt payloads must not escalate to page failure.
            log_warn("image: local image decode unavailable, keeping placeholder: %s",
                     img->source_path);
        }
    } else if (img->source_data) {
        // decode from memory buffer
        int width, height, channels;
        unsigned char* data = image_load_from_memory_scaled(img->source_data, img->source_data_len,
                                                            target_w, target_h, &width, &height, &channels);
        if (data) {
            image_surface_install_decoded_pixels(img, data, width, height);
            // Release builds strip debug logs; keep this opt-in trace for
            // cache-promotion tests without raising normal image decodes to note.
            if (image_decode_trace_enabled()) {
                log_notice("[image] Decoded HTTP image on demand: %dx%d (intrinsic %dx%d, target %dx%d)",
                           width, height, img->width, img->height, target_w, target_h);
            } else {
                log_debug("[image] Decoded HTTP image on demand: %dx%d (intrinsic %dx%d, target %dx%d)",
                          width, height, img->width, img->height, target_w, target_h);
            }
        } else {
            // Lazy decode happens after network metadata was accepted; keep
            // rendering stable when the payload is unsupported or corrupt.
            log_warn("image: HTTP image decode unavailable, keeping placeholder");
        }
        if (img->decoded_width >= img->width && img->decoded_height >= img->height) {
            radiant_clear_image_source_data(img);
        }
    }
}
