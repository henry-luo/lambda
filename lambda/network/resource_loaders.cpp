// resource_loaders.cpp
// Implementation of resource type handlers for network-loaded resources
// Integrates with CSS parser, image loader, native font system, and SVG parser

#include "resource_loaders.h"
#include "network_resource_manager.h"
#include "../../lib/log.h"
#include "../../lib/image.h"
#include "../../lib/file_utils.h"
#include "../../lib/mempool.h"
#include "../../lib/str.h"
#include "../input/css/dom_element.hpp"
#include "../input/css/css_parser.hpp"
#include "../input/css/css_engine.hpp"
#include "../input/css/css_font_face.hpp"
#include "../../radiant/view.hpp"
#include "../../radiant/layout.hpp"
#include "../../radiant/view.hpp"
#include "../../lib/font/font.h"
#include <string.h>
#include "../../lib/mem.h"
#include "../../lib/url.h"
#include <strings.h>  // for strcasecmp

// Helper: Read file contents into a string
static char* read_file_to_string(const char* path, size_t* out_size) {
    if (!path) return NULL;

    FILE* f = fopen(path, "rb");
    if (!f) {
        log_error("resource_loaders: failed to open file: %s", path);
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char* content = (char*)mem_alloc(size + 1, MEM_CAT_NETWORK);
    if (!content) {
        fclose(f);
        return NULL;
    }

    size_t read = fread(content, 1, size, f);
    fclose(f);

    if (read != size) {
        mem_free(content);
        return NULL;
    }

    content[size] = '\0';
    if (out_size) *out_size = size;
    return content;
}

static bool resource_url_has_extension(const char* url, const char* ext) {
    if (!url || !ext) return false;
    size_t ext_len = strlen(ext);
    const char* end = url + strlen(url);
    const char* query = strchr(url, '?');
    const char* fragment = strchr(url, '#');
    if (query && query < end) end = query;
    if (fragment && fragment < end) end = fragment;
    return (size_t)(end - url) >= ext_len &&
           strncasecmp(end - ext_len, ext, ext_len) == 0;
}

static bool resource_file_looks_like_svg(const char* path) {
    if (!path) return false;
    FILE* file = fopen(path, "rb");
    if (!file) return false;
    unsigned char probe[512];
    size_t size = fread(probe, 1, sizeof(probe), file);
    fclose(file);
    if (size == 0) return false;
    for (size_t i = 0; i + 4 < size; i++) {
        if (probe[i] == '<' &&
            strncasecmp((const char*)probe + i + 1, "svg", 3) == 0) {
            return true;
        }
    }
    return false;
}

// Helper: Add stylesheet to document
static bool add_stylesheet_to_document(DomDocument* doc, CssStylesheet* sheet) {
    if (!doc || !sheet) return false;

    if (doc->stylesheet_count >= doc->stylesheet_capacity) {
        int new_capacity = doc->stylesheet_capacity == 0 ? 4 : doc->stylesheet_capacity * 2;
        if (!doc->document_pool) {
            log_error("resource_loaders: cannot expand stylesheet array without document pool");
            return false;
        }

        // document stylesheet arrays are pool-owned after initial HTML load, so
        // network CSS must grow by copying instead of reallocating pool memory.
        size_t new_size = (size_t)new_capacity * sizeof(CssStylesheet*);
        CssStylesheet** new_sheets = (CssStylesheet**)pool_calloc(doc->document_pool, new_size);
        if (!new_sheets) {
            log_error("resource_loaders: failed to expand stylesheet array");
            return false;
        }
        if (doc->stylesheets && doc->stylesheet_count > 0) {
            memcpy(new_sheets, doc->stylesheets,
                   (size_t)doc->stylesheet_count * sizeof(CssStylesheet*));
        }
        doc->stylesheets = new_sheets;
        doc->stylesheet_capacity = new_capacity;
    }

    doc->stylesheets[doc->stylesheet_count++] = sheet;
    return true;
}

// CSS resource handler

// Helper: resolve a relative URL in a CssValue against a base URL
static void resolve_css_value_urls(CssValue* value, const Url* base_url, Pool* pool) {
    if (!value || !base_url || !pool) return;

    if (value->type == CSS_VALUE_TYPE_URL && value->data.url) {
        if (!url_is_absolute_url(value->data.url)) {
            Url* resolved = url_parse_with_base(value->data.url, base_url);
            if (resolved) {
                const char* href = url_get_href(resolved);
                if (href) {
                    value->data.url = pool_strdup(pool, href);
                }
                url_destroy(resolved);
            }
        }
    } else if (value->type == CSS_VALUE_TYPE_FUNCTION && value->data.function) {
        CssFunction* func = value->data.function;
        if (func->name && strcmp(func->name, "url") == 0) {
            for (int i = 0; i < func->arg_count; i++) {
                resolve_css_value_urls(func->args[i], base_url, pool);
            }
        }
    } else if (value->type == CSS_VALUE_TYPE_LIST && value->data.list.values) {
        for (int i = 0; i < value->data.list.count; i++) {
            resolve_css_value_urls(value->data.list.values[i], base_url, pool);
        }
    }
}

// Helper: walk all rules and resolve url() values against stylesheet origin
static void resolve_stylesheet_urls(CssStylesheet* sheet) {
    if (!sheet || !sheet->origin_url || !sheet->pool) return;

    // skip resolution for local file stylesheets
    if (!url_is_absolute_url(sheet->origin_url)) return;

    Url* base_url = url_parse(sheet->origin_url);
    if (!base_url) return;

    for (size_t i = 0; i < sheet->rule_count; i++) {
        CssRule* rule = sheet->rules[i];
        if (!rule) continue;

        // resolve urls in style rules
        if (rule->type == CSS_RULE_STYLE) {
            for (size_t j = 0; j < rule->data.style_rule.declaration_count; j++) {
                CssDeclaration* decl = rule->data.style_rule.declarations[j];
                if (decl && decl->value) {
                    resolve_css_value_urls(decl->value, base_url, sheet->pool);
                }
            }
        }
    }
    url_destroy(base_url);
}

void process_css_resource(NetworkResource* res, struct DomDocument* doc) {
    if (!res || (res->state != STATE_COMPLETED && res->state != STATE_CACHED) || !doc) return;

    log_debug("network: processing CSS resource %s from %s", res->url, res->local_path);

    // read CSS content from local file
    size_t css_size = 0;
    char* css_content = read_file_to_string(res->local_path, &css_size);
    if (!css_content) {
        log_error("network: failed to read CSS file: %s", res->local_path);
        return;
    }
    if (css_size == 0) {
        // Empty stylesheets are valid CSS; treat them as no-op resources so
        // zero-byte downloads do not surface as parser/read failures.
        mem_free(css_content);
        log_debug("network: skipping empty CSS resource: %s", res->url);
        return;
    }

    // get or create CSS engine
    CssEngine* engine = NULL;
    if (res->manager && res->manager->css_engine) {
        engine = (CssEngine*)res->manager->css_engine;
    } else if (doc->document_pool) {
        // create temporary engine if none available
        engine = css_engine_create(doc->document_pool);
        if (!engine) {
            log_error("network: failed to create CSS engine");
            mem_free(css_content);
            return;
        }
    } else {
        log_error("network: no CSS engine or pool available");
        mem_free(css_content);
        return;
    }

    // parse the stylesheet
    CssStylesheet* sheet = css_parse_stylesheet(engine, css_content, res->url);
    mem_free(css_content);  // content was copied by parser

    if (!sheet) {
        log_error("network: failed to parse CSS: %s", res->url);
        return;
    }

    log_debug("network: parsed CSS stylesheet with %zu rules", sheet->rule_count);

    // resolve relative url() values against the stylesheet's source URL
    resolve_stylesheet_urls(sheet);

    // add stylesheet to document
    if (!add_stylesheet_to_document(doc, sheet)) {
        log_error("network: failed to add stylesheet to document");
        return;
    }

    // schedule reflow for entire document (CSS affects all elements)
    if (res->manager && doc->root) {
        resource_manager_schedule_reflow(res->manager, doc->root);
    }

    log_debug("network: CSS resource processed successfully: %s", res->url);
}

// Image resource handler
void process_image_resource(NetworkResource* res, struct DomElement* img_element) {
    if (!res || (res->state != STATE_COMPLETED && res->state != STATE_CACHED) || !img_element) return;

    log_debug("network: processing image resource %s from %s", res->url, res->local_path);

    int img_width = 0;
    int img_height = 0;
    ImageSurface* img_surface = NULL;
    bool source_is_svg = resource_url_has_extension(res->url, ".svg");
    bool source_is_webp = resource_url_has_extension(res->url, ".webp");
    bool cached_file_is_svg = !source_is_svg && !source_is_webp &&
        resource_file_looks_like_svg(res->local_path);

    if ((source_is_svg || cached_file_is_svg) && res->manager && res->manager->ui_context) {
        // SVG resources, including extensionless URLs cached as .cache files,
        // must bypass raster probes that report ordinary SVG content as errors.
        img_surface = load_image((UiContext*)res->manager->ui_context, res->local_path);
        if (img_surface) {
            res->image_surface_borrowed = true;
        }
    }

    if (!img_surface && source_is_webp) {
        // WebP decoding is not available in the current image backend. Treat it
        // as a graceful placeholder instead of reporting a runtime load error.
        log_warn("network: unsupported WebP image, using placeholder: %s", res->url);
        if (res->manager) {
            resource_manager_schedule_repaint(res->manager, img_element);
        }
        return;
    }

    if (!img_surface && image_get_dimensions(res->local_path, &img_width, &img_height)) {
        img_surface = (ImageSurface*)mem_calloc(1, sizeof(ImageSurface), MEM_CAT_IMAGE);
        if (img_surface) {
            img_surface->width = img_width;
            img_surface->height = img_height;
            img_surface->source_path = mem_strdup(res->local_path, MEM_CAT_IMAGE);
            log_debug("network: image metadata loaded lazily: %dx%d from %s",
                      img_width, img_height, res->local_path);
        }
    }

    if (!img_surface) {
        // Header probing can fail for unusual formats. Fall back to a full
        // decode so the resource still has a chance to render.
        int channels = 0;
        unsigned char* data = image_load(res->local_path, &img_width, &img_height, &channels, 4);
        if (!data) {
            if (res->manager && res->manager->ui_context) {
                // Cached SVGs have hashed .cache names, so the generic raster
                // decoder cannot infer their format; use Radiant's content-aware
                // image loader and borrow the UI cache surface.
                img_surface = load_image((UiContext*)res->manager->ui_context, res->local_path);
                if (img_surface) {
                    res->image_surface_borrowed = true;
                }
            }
            if (!img_surface) {
                // Optional images can be corrupt or in an unsupported format;
                // keep the DOM alive and repaint with the broken-image path.
                log_warn("network: optional image unavailable: %s", res->local_path);
                // schedule repaint to show broken image indicator
                if (res->manager) {
                    resource_manager_schedule_repaint(res->manager, img_element);
                }
                return;
            }
        } else {
            log_debug("network: image decoded during fallback: %dx%d, channels=%d",
                      img_width, img_height, channels);

            img_surface = image_surface_create_from(img_width, img_height, data);
            if (!img_surface) {
                log_error("network: failed to create image surface: %s", res->url);
                image_free(data);
                return;
            }
        }
    }

    if (!img_surface) {
        log_error("network: failed to create image surface: %s", res->url);
        return;
    }

    // Preserve content-detected formats such as extensionless SVG cache files;
    // only raster fallback surfaces need URL-extension format inference.
    if (img_surface->format != IMAGE_FORMAT_SVG) {
        img_surface->format = IMAGE_FORMAT_PNG;  // default
        const char* ext = strrchr(res->url, '.');
        if (ext) {
            if (str_ieq_const(ext, strlen(ext), ".jpg") || str_ieq_const(ext, strlen(ext), ".jpeg")) {
                img_surface->format = IMAGE_FORMAT_JPEG;
            } else if (str_ieq_const(ext, strlen(ext), ".gif")) {
                img_surface->format = IMAGE_FORMAT_GIF;
            }
            // PNG is the default, WEBP not yet supported in ImageFormat enum
        }
    }

    // ensure element has embed property allocated
    if (!img_element->embed) {
        if (img_element->doc && img_element->doc->view_tree) {
            img_element->ensure_embed(img_element->doc->view_tree);
        } else if (img_element->doc && img_element->doc->document_pool) {
            // An async decode can finish before the first ViewTree exists; use
            // the document pool only at that lifetime seam and seed CSS initials.
            img_element->embed = (EmbedProp*)pool_calloc(img_element->doc->document_pool, sizeof(EmbedProp));
            if (img_element->embed) *img_element->embed = EMBED_PROP_DEFAULT;
        } else {
            img_element->embed = (EmbedProp*)mem_calloc(1, sizeof(EmbedProp), MEM_CAT_NETWORK);
            if (img_element->embed) *img_element->embed = EMBED_PROP_DEFAULT;
        }
        if (!img_element->embed) {
            log_error("network: failed to allocate embed property");
            image_surface_destroy(img_surface);
            return;
        }
    }

    if (res->image_surface && res->image_surface != img_surface && !res->image_surface_borrowed) {
        image_surface_destroy(res->image_surface);
    }
    res->image_surface = img_surface;

    // Async-loaded images are owned by the NetworkResource so teardown does not
    // depend on which DOM/view embed survives reflow.
    if (img_element->embed->img && img_element->embed->img != img_surface &&
            !img_element->embed->img->url && !res->image_surface_borrowed) {
        image_surface_destroy(img_element->embed->img);
    }
    img_element->embed->img = img_surface;

    // schedule reflow since image has intrinsic dimensions that affect layout
    if (res->manager) {
        resource_manager_schedule_reflow(res->manager, img_element);
    }

    log_debug("network: image resource processed successfully: %s", res->url);
}

// Font resource handler
void process_font_resource(NetworkResource* res, const struct CssFontFaceDescriptor* font_face) {
    if (!res || (res->state != STATE_COMPLETED && res->state != STATE_CACHED) || !font_face) return;

    log_debug("network: processing font resource %s from %s", res->url, res->local_path);

    // need UiContext for font system access
    if (!res->manager || !res->manager->ui_context) {
        log_error("network: no UI context available for font loading");
        return;
    }

    UiContext* uicon = (UiContext*)res->manager->ui_context;
    if (!uicon->font_ctx) {
        log_error("network: no font context available");
        return;
    }

    // map CSS weight/style → FontWeight/FontSlant (same as font_face.cpp)
    FontWeight fw = FONT_WEIGHT_NORMAL;
    if (font_face->font_weight == CSS_VALUE_BOLD)
        fw = FONT_WEIGHT_BOLD;
    else if (font_face->font_weight != CSS_VALUE_NORMAL &&
             font_face->font_weight >= 100 && font_face->font_weight <= 900)
        fw = (FontWeight)font_face->font_weight;

    FontSlant fs = FONT_SLANT_NORMAL;
    if (font_face->font_style == CSS_VALUE_ITALIC) fs = FONT_SLANT_ITALIC;
    else if (font_face->font_style == CSS_VALUE_OBLIQUE) fs = FONT_SLANT_OBLIQUE;

    // re-register @font-face with the downloaded local cache path as source
    // font_face_register() merges sources into existing entries
    FontFaceSource source = {};
    source.path = res->local_path;
    source.format = NULL;

    FontFaceDesc face_desc = {};
    face_desc.family = font_face->family_name ? font_face->family_name : "unknown";
    face_desc.weight = fw;
    face_desc.slant = fs;
    face_desc.sources = &source;
    face_desc.source_count = 1;

    if (font_face_register(uicon->font_ctx, &face_desc)) {
        log_debug("network: registered font local path for '%s': %s",
                  face_desc.family, res->local_path);
    }

    // schedule reflow for document to apply new font
    if (res->manager && res->manager->document) {
        DomDocument* doc = (DomDocument*)res->manager->document;
        if (doc->root) {
            resource_manager_schedule_reflow(res->manager, doc->root);
        }
    }

    log_debug("network: font resource processed successfully: %s (family: %s)",
              res->url, font_face->family_name ? font_face->family_name : "(unknown)");
}

// SVG resource handler (for <use xlink:href="external.svg#id">)
void process_svg_resource(NetworkResource* res, struct DomElement* use_element) {
    if (!res || (res->state != STATE_COMPLETED && res->state != STATE_CACHED) || !use_element) return;

    log_debug("network: processing SVG resource %s from %s", res->url, res->local_path);

    // read SVG file content
    size_t svg_size = 0;
    char* svg_content = read_file_to_string(res->local_path, &svg_size);
    if (!svg_content || svg_size == 0) {
        // Empty downloads still allocate a NUL buffer; free it before
        // treating the external SVG reference as unavailable.
        if (svg_content) mem_free(svg_content);
        log_error("network: failed to read SVG file: %s", res->local_path);
        return;
    }

    // extract fragment identifier from URL (e.g., "#icon-menu" -> "icon-menu")
    const char* fragment = strrchr(res->url, '#');
    const char* target_id = fragment ? fragment + 1 : NULL;

    if (!target_id || strlen(target_id) == 0) {
        log_warn("network: SVG use element requires fragment ID: %s", res->url);
        // still store the SVG content for potential full-document use
    }

    // for now, we store the SVG content for later processing by the layout engine
    // full implementation would:
    // 1. parse SVG as XML/HTML
    // 2. find element with matching id
    // 3. clone that subtree into the <use> element's shadow DOM
    // 4. trigger layout

    // store reference to SVG file path on the use element for layout-time processing
    // this allows the SVG to be loaded when the <use> element is laid out

    log_debug("network: SVG resource loaded, target_id=%s, size=%zu bytes",
              target_id ? target_id : "(none)", svg_size);

    mem_free(svg_content);

    // schedule reflow so the <use> element can incorporate the SVG
    if (res->manager) {
        resource_manager_schedule_reflow(res->manager, use_element);
    }

    log_debug("network: SVG resource processed: %s", res->url);
}

// HTML resource handler
void process_html_resource(NetworkResource* res, struct DomDocument* doc) {
    if (!res || (res->state != STATE_COMPLETED && res->state != STATE_CACHED) || !doc) return;

    log_debug("network: processing HTML resource %s from %s", res->url, res->local_path);

    // HTML is typically the main document and loaded at initialization,
    // not as a sub-resource. This handler exists for potential future use
    // cases like iframe loading or prefetch scenarios.

    // for sub-document loading (iframes):
    // 1. parse HTML from res->local_path
    // 2. build DomDocument for the iframe
    // 3. attach to parent document
    // 4. trigger layout of iframe element

    log_info("network: HTML resource available at: %s", res->local_path);
}

// Script resource handler
// Downloaded scripts are stored in cache — execution happens via script_runner
// when the document reaches script execution phase (or on late arrival)
void process_script_resource(NetworkResource* res, struct DomDocument* doc) {
    if (!res || (res->state != STATE_COMPLETED && res->state != STATE_CACHED) || !doc) return;

    log_info("network: script resource downloaded: %s -> %s", res->url, res->local_path);

    // Read the downloaded script content
    if (!res->local_path) {
        log_warn("network: script resource has no local_path: %s", res->url);
        return;
    }
    size_t content_size = 0;
    char* content = read_file_to_string(res->local_path, &content_size);
    if (!content || content_size == 0) {
        log_warn("network: failed to read cached script: %s", res->local_path);
        if (content) mem_free(content);
        return;
    }

    // Late-arriving scripts are logged but not executed on the worker thread.
    // Execution requires the main thread (JIT compiler, DOM access).
    // The script content is available in the cache file for later execution
    // via flush_layout_updates() or a future incremental script runner pass.
    log_info("network: script cached for deferred execution: %s (%zu bytes)", res->url, content_size);
    mem_free(content);

    // Schedule reflow so the main thread can pick up and execute this script
    if (res->manager && doc->root) {
        resource_manager_schedule_reflow(res->manager, doc->root);
    }
}

// Resource failure handler
void handle_resource_failure(NetworkResource* res, struct DomDocument* doc) {
    if (!res || !doc) return;

    log_warn("network: handling resource failure: %s (%s)",
             res->url, res->error_message ? res->error_message : "unknown error");

    switch (res->type) {
        case RESOURCE_HTML:
            log_error("network: HTML load failed: %s", res->url);
            // main document failure is critical
            doc->fully_loaded = true;  // mark as "done" even though failed
            break;

        case RESOURCE_CSS:
            log_warn("network: CSS load failed: %s (continuing without stylesheet)", res->url);
            // document continues rendering without this stylesheet
            // styles will use defaults or other available stylesheets
            break;

        case RESOURCE_IMAGE:
            log_warn("network: Image load failed: %s", res->url);
            // schedule repaint to show broken image indicator
            if (res->owner_element && res->manager) {
                resource_manager_schedule_repaint(res->manager, (DomElement*)res->owner_element);
            }
            break;

        case RESOURCE_FONT:
            log_warn("network: Font load failed: %s (using fallback)", res->url);
            // fallback font will be used automatically by font matching
            // no need to explicitly schedule reflow - current font cascade handles this
            break;

        case RESOURCE_SVG:
            log_warn("network: SVG load failed: %s", res->url);
            // <use> element remains empty or shows fallback
            if (res->owner_element && res->manager) {
                resource_manager_schedule_repaint(res->manager, (DomElement*)res->owner_element);
            }
            break;

        case RESOURCE_SCRIPT:
            log_warn("network: Script load failed: %s", res->url);
            // Script won't execute
            break;
    }
}
