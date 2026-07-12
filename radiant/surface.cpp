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
#include "../lib/hashmap_helpers.h"
#include "../lib/memtrack.h"
#include "../lib/base64.h"
#include "../lib/url.h"
#include "../lib/file.h"
#include "../lambda/input/input.hpp"  // for download_http_content
#include "../lambda/network/network_resource_manager.h"

#include <stdlib.h>
#include <unistd.h>
#include <strings.h>

typedef struct ImageEntry {
    // ImageFormat format;
    const char* path;  // todo: change to URL
    ImageSurface *image;
} ImageEntry;

HASHMAP_DEFINE_STRKEY(image, ImageEntry, path)

static char* try_join_absolute_resource(const char* root, const char* abs_path) {
    if (!root || !abs_path || abs_path[0] != '/') return nullptr;
    size_t root_len = strlen(root);
    size_t path_len = strlen(abs_path);
    char* candidate = (char*)mem_alloc(root_len + path_len + 1, MEM_CAT_RENDER);
    if (!candidate) return nullptr;
    memcpy(candidate, root, root_len);
    memcpy(candidate + root_len, abs_path, path_len);
    candidate[root_len + path_len] = '\0';
    if (file_exists(candidate)) return candidate;
    mem_free(candidate);
    return nullptr;
}

static char* resolve_wpt_absolute_image_path(UiContext* uicon, const char* img_url) {
    if (!uicon || !uicon->document || !uicon->document->url ||
        !img_url || img_url[0] != '/' || img_url[1] == '/') {
        return nullptr;
    }

    char* doc_path = url_to_local_path(uicon->document->url);
    if (doc_path) {
        const char* wpt_marker = strstr(doc_path, "/ref/wpt/");
        if (wpt_marker) {
            size_t root_len = (size_t)(wpt_marker - doc_path) + strlen("/ref/wpt");
            char* root = (char*)mem_alloc(root_len + 1, MEM_CAT_RENDER);
            if (root) {
                memcpy(root, doc_path, root_len);
                root[root_len] = '\0';
                char* resolved = try_join_absolute_resource(root, img_url);
                mem_free(root);
                if (resolved) {
                    mem_free(doc_path);
                    return resolved;
                }
            }
        }

        const char* data_marker = strstr(doc_path, "/layout/data/");
        if (data_marker) {
            size_t root_len = (size_t)(data_marker - doc_path) + strlen("/layout/data");
            char* root = (char*)mem_alloc(root_len + strlen("/support") + 1, MEM_CAT_RENDER);
            if (root) {
                memcpy(root, doc_path, root_len);
                memcpy(root + root_len, "/support", strlen("/support") + 1);
                char* resolved = try_join_absolute_resource(root, img_url);
                mem_free(root);
                if (resolved) {
                    mem_free(doc_path);
                    return resolved;
                }
            }
        }
        mem_free(doc_path);
    }

    char cwd[4096];
    if (getcwd(cwd, sizeof(cwd))) {
        char* wpt_root = try_join_absolute_resource(cwd, "/ref/wpt");
        if (wpt_root) {
            char* resolved = try_join_absolute_resource(wpt_root, img_url);
            mem_free(wpt_root);
            return resolved;
        }

        size_t cwd_len = strlen(cwd);
        const char* support_root_suffix = "/test/layout/data/support";
        size_t suffix_len = strlen(support_root_suffix);
        char* support_root = (char*)mem_alloc(cwd_len + suffix_len + 1, MEM_CAT_RENDER);
        if (!support_root) return nullptr;
        memcpy(support_root, cwd, cwd_len);
        memcpy(support_root + cwd_len, support_root_suffix, suffix_len + 1);
        char* resolved = try_join_absolute_resource(support_root, img_url);
        mem_free(support_root);
        return resolved;
    }

    return nullptr;
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

typedef struct SvgImageIntrinsicMetadata {
    float width;
    float height;
    bool has_width;
    bool has_height;
    bool has_ratio;
} SvgImageIntrinsicMetadata;

static const char* svg_root_tag_end(const char* svg, const char* end) {
    if (!svg || !end) return NULL;

    const char* tag_end = svg;
    char quote = 0;
    while (tag_end < end) {
        char c = *tag_end;
        if (quote) {
            if (c == quote) quote = 0;
        } else if (c == '"' || c == '\'') {
            quote = c;
        } else if (c == '>') {
            return tag_end;
        }
        tag_end++;
    }
    return NULL;
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
            memcpy(out, value_start, value_len);
            out[value_len] = '\0';
            return true;
        }
    }

    return false;
}

static bool svg_parse_number_token(const char** cursor, float* out_value) {
    if (!cursor || !*cursor || !out_value) return false;

    const char* p = *cursor;
    while (*p && (isspace((unsigned char)*p) || *p == ',')) p++;
    char* end_ptr = NULL;
    float value = strtof(p, &end_ptr);
    if (end_ptr == p) return false;
    *out_value = value;
    *cursor = end_ptr;
    return true;
}

static bool svg_parse_viewbox_attr(const char* value, float* width, float* height) {
    if (!value || !width || !height) return false;

    const char* p = value;
    float min_x = 0.0f;
    float min_y = 0.0f;
    float vb_width = 0.0f;
    float vb_height = 0.0f;
    if (!svg_parse_number_token(&p, &min_x)) return false;
    if (!svg_parse_number_token(&p, &min_y)) return false;
    if (!svg_parse_number_token(&p, &vb_width)) return false;
    if (!svg_parse_number_token(&p, &vb_height)) return false;
    if (vb_width <= 0.0f || vb_height <= 0.0f) return false;

    *width = vb_width;
    *height = vb_height;
    return true;
}

static bool svg_parse_definite_length_attr(const char* value, float* out_value) {
    if (!value || !out_value) return false;

    const char* p = value;
    while (*p && isspace((unsigned char)*p)) p++;
    char* end_ptr = NULL;
    float length = strtof(p, &end_ptr);
    if (end_ptr == p || length <= 0.0f) return false;

    while (*end_ptr && isspace((unsigned char)*end_ptr)) end_ptr++;
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
    const char* svg = find_bytes(data, size, "<svg", 4);
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
    surface->generation = 1;
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

static void load_image_cleanup_failed(Url* abs_url, char* file_path, unsigned char* downloaded_data) {
    if (downloaded_data) mem_free(downloaded_data);
    if (file_path) mem_free(file_path);
    if (abs_url) url_destroy(abs_url);
}

static bool image_path_has_declared_non_svg_extension(const char* file_path) {
    if (!file_path) return false;
    const char* slash = strrchr(file_path, '/');
    const char* dot = strrchr(file_path, '.');
    if (!dot || (slash && dot < slash)) return false;
    // cached network resources keep a synthetic suffix, so sniff their bytes for SVG.
    if (strcasecmp(dot, ".cache") == 0) return false;
    if (strcasecmp(dot, ".svg") == 0 || strcasecmp(dot, ".svgz") == 0) return false;
    return true;
}

ImageSurface* load_image(UiContext* uicon, const char *img_url) {
    if (uicon->document == NULL || uicon->document->url == NULL) {
        log_error("Missing URL context for image: %s", img_url);
        return NULL;
    }

    if (uicon->image_cache == NULL) {
        // create a new hash map. 2nd argument is the initial capacity.
        // 3rd and 4th arguments are optional seeds that are passed to the following hash function.
        uicon->image_cache = image_new(10);
    }

    // Handle data: URIs
    if (strncmp(img_url, "data:", 5) == 0) {
        ImageEntry search_key = {.path = (char*)img_url, .image = NULL};
        ImageEntry* entry = (ImageEntry*) hashmap_get(uicon->image_cache, &search_key);
        if (entry) {
            log_debug("[BG-IMAGE] Data URI image loaded from cache");
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
            log_warn("image: data URI payload unavailable, using placeholder");
            return NULL;
        }
        // Detect format from MIME type or content
        bool is_svg = is_svg_content(decoded, decoded_len);
        ImageSurface* surface;
        if (is_svg) {
            SvgImageIntrinsicMetadata svg_meta =
                svg_read_intrinsic_metadata_in_memory((const char*)decoded, decoded_len);
            surface = (ImageSurface*)mem_calloc(1, sizeof(ImageSurface), MEM_CAT_IMAGE);
            surface->format = IMAGE_FORMAT_SVG;
            surface->pic = rdt_picture_load_data((const char*)decoded, (int)decoded_len, "svg");
            if (!surface->pic) {
                mem_free(decoded);
                mem_free(surface);
                return NULL;
            }
            float svg_w, svg_h;
            rdt_picture_get_size(surface->pic, &svg_w, &svg_h);
            image_surface_apply_svg_metadata(surface, svg_meta, svg_w, svg_h);
            mem_free(decoded);
        } else {
            int width, height, channels;
            int orientation = jpeg_exif_orientation_from_memory(decoded, decoded_len);
            if (!image_get_dimensions_from_memory(decoded, decoded_len, &width, &height)) {
                // Invalid inline payloads are common in scraped pages; probe
                // before full decode so placeholders do not emit backend errors.
                mem_free(decoded);
                log_warn("image: unsupported data URI image, using placeholder");
                return NULL;
            }
            unsigned char* data = image_load_from_memory(decoded, decoded_len, &width, &height, &channels);
            mem_free(decoded);
            if (!data) {
                log_warn("image: unsupported data URI image, using placeholder");
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
        char* cache_path = mem_strdup(img_url, MEM_CAT_RENDER);
        if (!cache_path) {
            image_surface_destroy(surface);
            return NULL;
        }
        // Inline image surfaces used to escape the shared cache, so shutdown
        // had no owner to release their decoded pixels.
        surface->cache_owned = true;
        ImageEntry new_entry = {.path = (char*)cache_path, .image = surface};
        hashmap_set(uicon->image_cache, &new_entry);
        log_debug("[BG-IMAGE] Loaded data URI image: %dx%d", surface->width, surface->height);
        return surface;
    }
    const char* resolved_img_url = img_url;
    char* local_file_url = nullptr;
    if (file_exists(img_url)) {
        char abs_path[4096];
        if (img_url[0] == '/') {
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
            local_file_url = url_from_local_path(abs_path);
            if (local_file_url) resolved_img_url = local_file_url;
        }
    }

    Url* abs_url = parse_url(uicon->document->url, resolved_img_url);
    if (local_file_url) mem_free(local_file_url);
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
        // Network-managed documents cannot let layout/render perform blocking
        // HTTP fetches; failed or late-discovered images fall back to a missing
        // image instead of stalling shutdown for per-image timeouts.
        if (uicon->document->resource_manager) {
            log_debug("[image] Skipping sync HTTP image fetch (resource manager active): %s",
                      url_get_href(abs_url));
            url_destroy(abs_url);
            return NULL;
        }
        // Download the image from HTTP URL
        const char* url_str = url_get_href(abs_url);
        file_path = mem_strdup(url_str, MEM_CAT_RENDER);
        if (!file_path) {
            url_destroy(abs_url);
            return NULL;
        }
        ImageEntry search_key = {.path = (char*)file_path, .image = NULL};
        ImageEntry* entry = (ImageEntry*) hashmap_get(uicon->image_cache, &search_key);
        if (entry) {
            log_debug("Image loaded from cache: %s", file_path);
            mem_free(file_path);
            url_destroy(abs_url);
            return entry->image;
        }
        log_debug("[image] Downloading image from URL: %s", url_str);
        downloaded_data = (unsigned char*)download_http_content(url_str, &downloaded_size, nullptr);
        if (!downloaded_data || downloaded_size == 0) {
            log_error("[image] Failed to download image: %s", url_str);
            if (downloaded_data) mem_free(downloaded_data);
            mem_free(file_path);
            url_destroy(abs_url);
            return NULL;
        }
        log_debug("[image] Downloaded image: %zu bytes", downloaded_size);
    } else {
        file_path = url_to_local_path(abs_url);
        if (!file_path) {
            log_error("Invalid local URL: %s", img_url);
            url_destroy(abs_url);
            return NULL;
        }
        if (img_url[0] == '/' && img_url[1] != '/' && !file_exists(file_path)) {
            // Local WPT runs emulate an HTTP server; URL-absolute resources are
            // rooted at the WPT tree, not at the host filesystem root.
            char* wpt_path = resolve_wpt_absolute_image_path(uicon, img_url);
            if (wpt_path) {
                log_debug("[image] Resolved WPT absolute resource %s -> %s", img_url, wpt_path);
                mem_free(file_path);
                file_path = wpt_path;
            }
        }
    }

    ImageEntry search_key = {.path = (char*)file_path, .image = NULL};
    ImageEntry* entry = (ImageEntry*) hashmap_get(uicon->image_cache, &search_key);
    if (entry) {
        log_debug("Image loaded from cache: %s", file_path);
        // HTTP cache lookup normally happens before download; keep this cleanup
        // for race/fallback paths so a cached surface never drops a fresh buffer.
        if (downloaded_data) mem_free(downloaded_data);
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
        if (!is_svg && !image_path_has_declared_non_svg_extension(file_path)) {
            FILE* svg_probe = fopen(file_path, "rb");
            if (svg_probe) {
                unsigned char probe_buf[512];
                size_t probe_size = fread(probe_buf, 1, sizeof(probe_buf), svg_probe);
                fclose(svg_probe);
                // Network cache files do not preserve extensions; declared
                // .png/.jpg resources must keep browser-like type handling.
                is_svg = is_svg_content(probe_buf, probe_size);
            }
        }
    }

    if (is_svg) {
        SvgImageIntrinsicMetadata svg_meta = is_http && downloaded_data
            ? svg_read_intrinsic_metadata_in_memory((const char*)downloaded_data, downloaded_size)
            : svg_read_intrinsic_metadata_in_file(file_path);
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
            load_image_cleanup_failed(abs_url, file_path, downloaded_data);
            return NULL;
        }
        float svg_w, svg_h;
        rdt_picture_get_size(surface->pic, &svg_w, &svg_h);
        image_surface_apply_svg_metadata(surface, svg_meta, svg_w, svg_h);
        log_debug("SVG image size: %d x %d (picture %.1f x %.1f, intrinsic=%d)",
                  surface->width, surface->height, svg_w, svg_h, surface->has_intrinsic_size);
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
        downloaded_data = nullptr;
        if (!surface) {
            load_image_cleanup_failed(abs_url, file_path, nullptr);
            return NULL;
        }
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
                downloaded_data = nullptr;
                if (!data) {
                    log_debug("failed to load image: %s", file_path);
                    load_image_cleanup_failed(abs_url, file_path, nullptr);
                    return NULL;
                }
                surface = image_surface_create_from(width, height, data);
                if (!surface) {
                    image_free(data);
                    load_image_cleanup_failed(abs_url, file_path, nullptr);
                    return NULL;
                }
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
                    load_image_cleanup_failed(abs_url, file_path, nullptr);
                    return NULL;
                }
                surface = image_surface_create_from(width, height, data);
                if (!surface) {
                    image_free(data);
                    load_image_cleanup_failed(abs_url, file_path, nullptr);
                    return NULL;
                }
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
    surface->cache_owned = true;
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

static bool image_decode_trace_enabled(void) {
    const char* trace = getenv("LAMBDA_IMAGE_DECODE_TRACE");
    return trace && trace[0] != '\0';
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
            if (img->pixels) {
                mem_free(img->pixels);
            }
            img->pixels = data;
            img->decoded_width = width;
            img->decoded_height = height;
            img->pitch = width * 4;
            image_surface_bump_generation(img);
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
            mem_free(img->source_data);
            radiant_clear_image_source_data(img);
        }
    }
}
