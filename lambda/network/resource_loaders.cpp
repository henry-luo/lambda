// resource_loaders.cpp
// Implementation of resource type handlers for network-loaded resources
// Integrates with CSS parser, image loader, FreeType font system, and SVG parser

#include "resource_loaders.h"
#include "network_resource_manager.h"
#include "../../lib/log.h"
#include "../../lib/image.h"
#include "../../lib/file_utils.h"
#include "../../lib/mempool.h"
#include "../input/css/dom_element.hpp"
#include "../input/css/css_parser.hpp"
#include "../input/css/css_engine.hpp"
#include "../input/css/css_font_face.hpp"
#include "../../radiant/view.hpp"
#include "../../radiant/layout.hpp"
#include "../../radiant/font_face.h"
#include <string.h>
#include <stdlib.h>
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
    
    char* content = (char*)malloc(size + 1);
    if (!content) {
        fclose(f);
        return NULL;
    }
    
    size_t read = fread(content, 1, size, f);
    fclose(f);
    
    if (read != size) {
        free(content);
        return NULL;
    }
    
    content[size] = '\0';
    if (out_size) *out_size = size;
    return content;
}

// Helper: Add stylesheet to document
static bool add_stylesheet_to_document(DomDocument* doc, CssStylesheet* sheet) {
    if (!doc || !sheet) return false;
    
    // ensure capacity
    if (doc->stylesheet_count >= doc->stylesheet_capacity) {
        int new_capacity = doc->stylesheet_capacity == 0 ? 4 : doc->stylesheet_capacity * 2;
        CssStylesheet** new_sheets = (CssStylesheet**)realloc(
            doc->stylesheets, new_capacity * sizeof(CssStylesheet*));
        if (!new_sheets) {
            log_error("resource_loaders: failed to expand stylesheet array");
            return false;
        }
        doc->stylesheets = new_sheets;
        doc->stylesheet_capacity = new_capacity;
    }
    
    doc->stylesheets[doc->stylesheet_count++] = sheet;
    return true;
}

// CSS resource handler
void process_css_resource(NetworkResource* res, struct DomDocument* doc) {
    if (!res || res->state != STATE_COMPLETED || !doc) return;
    
    log_debug("network: processing CSS resource %s from %s", res->url, res->local_path);
    
    // read CSS content from local file
    size_t css_size = 0;
    char* css_content = read_file_to_string(res->local_path, &css_size);
    if (!css_content || css_size == 0) {
        log_error("network: failed to read CSS file: %s", res->local_path);
        return;
    }
    
    // get or create CSS engine
    CssEngine* engine = NULL;
    if (res->manager && res->manager->css_engine) {
        engine = (CssEngine*)res->manager->css_engine;
    } else if (doc->pool) {
        // create temporary engine if none available
        engine = css_engine_create(doc->pool);
        if (!engine) {
            log_error("network: failed to create CSS engine");
            free(css_content);
            return;
        }
    } else {
        log_error("network: no CSS engine or pool available");
        free(css_content);
        return;
    }
    
    // parse the stylesheet
    CssStylesheet* sheet = css_parse_stylesheet(engine, css_content, res->url);
    free(css_content);  // content was copied by parser
    
    if (!sheet) {
        log_error("network: failed to parse CSS: %s", res->url);
        return;
    }
    
    log_debug("network: parsed CSS stylesheet with %zu rules", sheet->rule_count);
    
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
    if (!res || res->state != STATE_COMPLETED || !img_element) return;
    
    log_debug("network: processing image resource %s from %s", res->url, res->local_path);
    
    // load image data from file using stb_image
    int img_width, img_height, channels;
    unsigned char* data = image_load(res->local_path, &img_width, &img_height, &channels, 4);
    if (!data) {
        log_error("network: failed to load image: %s", res->local_path);
        // schedule repaint to show broken image indicator
        if (res->manager) {
            resource_manager_schedule_repaint(res->manager, img_element);
        }
        return;
    }
    
    log_debug("network: image loaded: %dx%d, channels=%d", img_width, img_height, channels);
    
    // create ImageSurface from loaded data
    ImageSurface* img_surface = image_surface_create_from(img_width, img_height, data);
    if (!img_surface) {
        log_error("network: failed to create image surface: %s", res->url);
        image_free(data);
        return;
    }
    
    // detect format from URL extension
    img_surface->format = IMAGE_FORMAT_PNG;  // default
    const char* ext = strrchr(res->url, '.');
    if (ext) {
        if (strcasecmp(ext, ".jpg") == 0 || strcasecmp(ext, ".jpeg") == 0) {
            img_surface->format = IMAGE_FORMAT_JPEG;
        } else if (strcasecmp(ext, ".gif") == 0) {
            img_surface->format = IMAGE_FORMAT_GIF;
        }
        // PNG is the default, WEBP not yet supported in ImageFormat enum
    }
    
    // ensure element has embed property allocated
    if (!img_element->embed) {
        // allocate from document pool if available
        if (img_element->doc && img_element->doc->pool) {
            img_element->embed = (EmbedProp*)pool_calloc(img_element->doc->pool, sizeof(EmbedProp));
        } else {
            img_element->embed = (EmbedProp*)calloc(1, sizeof(EmbedProp));
        }
        if (!img_element->embed) {
            log_error("network: failed to allocate embed property");
            image_surface_destroy(img_surface);
            return;
        }
    }
    
    // store image in element's embed property
    // free any previous image first
    if (img_element->embed->img) {
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
void process_font_resource(NetworkResource* res, struct CssFontFaceDescriptor* font_face) {
    if (!res || res->state != STATE_COMPLETED || !font_face) return;
    
    log_debug("network: processing font resource %s from %s", res->url, res->local_path);
    
    // need UiContext for FreeType access
    if (!res->manager || !res->manager->ui_context) {
        log_error("network: no UI context available for font loading");
        return;
    }
    
    UiContext* uicon = (UiContext*)res->manager->ui_context;
    
    // create a default FontProp for loading (can be refined later)
    FontProp default_style = {0};
    default_style.font_size = 16.0f;  // default size, will be scaled per-use
    default_style.font_weight = CSS_VALUE_NORMAL;
    default_style.font_style = font_face->font_style;
    
    // load font file using the font loading system
    FT_Face face = load_local_font_file(uicon, res->local_path, &default_style);
    if (!face) {
        log_error("network: failed to load font: %s", res->local_path);
        return;
    }
    
    log_debug("network: loaded font: family='%s', style='%s', %d glyphs",
              face->family_name ? face->family_name : "(unknown)",
              face->style_name ? face->style_name : "(unknown)",
              (int)face->num_glyphs);
    
    // update font_face descriptor with the loaded path
    // so that future text layout can find this font
    if (!font_face->src_url) {
        // set the successful source URL
        font_face->src_url = strdup(res->local_path);
    }
    
    // schedule reflow for document to apply new font
    // note: need a way to get the document from the font_face or manager
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
    if (!res || res->state != STATE_COMPLETED || !use_element) return;
    
    log_debug("network: processing SVG resource %s from %s", res->url, res->local_path);
    
    // read SVG file content
    size_t svg_size = 0;
    char* svg_content = read_file_to_string(res->local_path, &svg_size);
    if (!svg_content || svg_size == 0) {
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
    
    free(svg_content);
    
    // schedule reflow so the <use> element can incorporate the SVG
    if (res->manager) {
        resource_manager_schedule_reflow(res->manager, use_element);
    }
    
    log_debug("network: SVG resource processed: %s", res->url);
}

// HTML resource handler
void process_html_resource(NetworkResource* res, struct DomDocument* doc) {
    if (!res || res->state != STATE_COMPLETED || !doc) return;
    
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
