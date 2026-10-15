#include "view.hpp"
#include "layout.hpp"
#include "radiant.hpp"
#include "../lambda/dom/dom.h"
#include "../lambda/dom/dom_engine.h"
#include "../lambda/input/css/dom_element.hpp"
#include "../lambda/input/css/dom_lifecycle.hpp"
#include "../lambda/network/network_resource_manager.h"
#include "../lib/url.h"
#include "../lib/mem.h"
#include <limits.h>

struct DomImageSource : DomDocumentResourceData {
    DomImageSource* next;
    DomDocument* document;
    DomNodeRef node;
    lam::Handle<ImageSurface> surface;
    NetworkResource* pending;
    char* pending_url;
};
static DomImageSource* image_sources;
static bool image_source_pending(DomDocumentResourceData* data);
static void image_source_destroy(DomDocumentResourceData* data) {
    auto* source=(DomImageSource*)data;
    auto** link=&image_sources;while(*link&&*link!=source) link=&(*link)->next;
    if(*link) *link=source->next;
    mem_free(source->pending_url); mem_free(source);
}
static DomImageSource* image_source_for(DomElement* element,bool create) {
    unsigned count=0;
    for(auto* source=image_sources;source;) {
        auto* next=source->next;
        // registry generations, rather than stale node bytes, decide whether a cache entry can survive.
        if(!dom_node_ref_validate(source->document,source->node)) {
            dom_document_release_resource(source->document,source);source=next;continue;
        }
        if(source->document==element->doc&&source->node.address==element&&source->node.expected_id==element->DomNode::id) return source;
        count++;source=next;
    }
    if(!create||count>=4096) return nullptr;
    auto* source=(DomImageSource*)mem_calloc(1,sizeof(DomImageSource),MEM_CAT_RENDER);if(!source) return nullptr;
    source->document=element->doc;source->node=dom_node_ref(element);
    if(!dom_document_add_resource(element->doc,source,image_source_destroy)) {mem_free(source);return nullptr;}
    dom_document_set_resource_pending_work(element->doc, source, image_source_pending);
    source->next=image_sources;image_sources=source;return source;
}
ImageSurface* image_element_surface(DomElement* element) {
    if(!element||!element->doc||element->tag()!=MARKUP_NAME_IMG) return nullptr;
    DomImageSource* source=image_source_for(element,false);
    // intrinsic loader state outlives layout's EmbedProp, including detached TextureLoader images.
    if(source) return dom_node_ref_validate(element->doc,source->node)?image_surface_lookup(source->surface):nullptr;
    return element->embed?element->embed->img:nullptr;
}
static bool image_source_pending(DomDocumentResourceData* data) {
    auto* source = (DomImageSource*)data;
    if (!source->pending) return false;
    DomDocument* doc = source->document;
    DomNode* node = dom_node_ref_validate(doc, source->node);
    if (!node || !doc->resource_manager) { source->pending = nullptr; return false; }
    resource_manager_flush_layout_updates(doc->resource_manager);
    ResourceState state = source->pending->state;
    if (state != STATE_COMPLETED && state != STATE_CACHED && state != STATE_FAILED) return true;
    source->pending = nullptr;
    DomElement* element = node->as_element();
    auto* ui = (UiContext*)doc->js.host_ui_context;
    ImageSurface* image = state != STATE_FAILED && ui ? load_document_image(doc, ui, source->pending_url) : nullptr;
    source->surface = image ? image->self : lam::Handle<ImageSurface>{};
    if (element->embed) element->embed->img = lam::up(image);
    mem_free(source->pending_url); source->pending_url = nullptr;
    dom_queue_image_completion(element, image != nullptr);
    return false;
}

extern "C" bool dom_engine_set_image_source(DomElement* element,const char* name, bool* pending) {
    if (pending) *pending = false;
    if(!element||!element->doc||element->tag()!=MARKUP_NAME_IMG||!name) return false;
    DomImageSource* source=image_source_for(element,true);if(!source) return false;
    source->pending = nullptr; mem_free(source->pending_url); source->pending_url = nullptr;
    source->surface={};if(element->embed) element->embed->img=nullptr;
    auto* ui=(UiContext*)element->doc->js.host_ui_context;if(!ui) return false;
    if (element->doc->resource_manager && document_dependency_admits(element->doc, name)) {
        Url* absolute = parse_url(element->doc->url, name);
        if (absolute && radiant_url_is_http(absolute)) {
            NetworkResource* resource = resource_manager_load(element->doc->resource_manager,
                url_get_href(absolute), RESOURCE_IMAGE, PRIORITY_NORMAL, nullptr);
            if (resource && resource->state != STATE_COMPLETED && resource->state != STATE_CACHED &&
                resource->state != STATE_FAILED) {
                source->pending_url = mem_strdup(url_get_href(absolute), MEM_CAT_RENDER);
                if (source->pending_url) { source->pending = resource; if (pending) *pending = true; }
                url_destroy(absolute);
                return false;
            }
        }
        if (absolute) url_destroy(absolute);
    }
    ImageSurface* image=load_document_image(element->doc,ui,name);
    if(!image) return false;
    source->surface=image->self;if(element->embed) element->embed->img=lam::up(image);
    return true;
}
extern "C" bool dom_engine_image_natural_size(DomElement* element,int* width,int* height) {
    if(width) *width=0;if(height) *height=0;
    ImageSurfaceReadScope scope;ImageSurface* image=image_element_surface(element);
    if(!image||!image->has_intrinsic_size||image->width<=0||image->height<=0) return false;
    if(width) *width=image->width;if(height) *height=image->height;return true;
}
extern "C" bool dom_engine_image_rendered_size(DomElement* element,int* width,int* height) {
    if(!element||element->tag()!=MARKUP_NAME_IMG||!dom_is_connected(element)||!element->is_block()) return false;
    for(DomNode* ancestor=element;ancestor;ancestor=ancestor->parent)
        if(ancestor->is_element()&&ancestor->as_element()->display.outer==CSS_VALUE_NONE) return false;
    LayoutContentBox box=layout_content_box((ViewBlock*)element);
    if(!isfinite(box.width)||!isfinite(box.height)||box.width<0||box.height<0||box.width>INT_MAX||box.height>INT_MAX) return false;
    if(width) *width=(int)floorf(box.width+0.5f); // INT_CAST_OK: HTMLImageElement width is an integer CSS-pixel IDL value.
    if(height) *height=(int)floorf(box.height+0.5f); // INT_CAST_OK: HTMLImageElement height is an integer CSS-pixel IDL value.
    return true;
}
