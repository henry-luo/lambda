#include "view.hpp"
#include "layout.hpp"
#include "radiant.hpp"
#include "../lambda/dom/dom.h"
#include "../lambda/dom/dom_engine.h"
#include "../lambda/input/css/dom_element.hpp"
#include "../lambda/input/css/dom_lifecycle.hpp"
#include "../lib/mem.h"
#include <limits.h>

struct DomImageSource : DomDocumentResourceData {
    DomImageSource* next;
    DomDocument* document;
    DomNodeRef node;
    lam::Handle<ImageSurface> surface;
};
static DomImageSource* image_sources;
static void image_source_destroy(DomDocumentResourceData* data) {
    auto* source=(DomImageSource*)data;
    auto** link=&image_sources;while(*link&&*link!=source) link=&(*link)->next;
    if(*link) *link=source->next;
    mem_free(source);
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
    source->next=image_sources;image_sources=source;return source;
}
ImageSurface* image_element_surface(DomElement* element) {
    if(!element||!element->doc||element->tag()!=MARKUP_NAME_IMG) return nullptr;
    DomImageSource* source=image_source_for(element,false);
    // intrinsic loader state outlives layout's EmbedProp, including detached TextureLoader images.
    if(source) return dom_node_ref_validate(element->doc,source->node)?image_surface_lookup(source->surface):nullptr;
    return element->embed?element->embed->img:nullptr;
}
extern "C" bool dom_engine_set_image_source(DomElement* element,const char* name) {
    if(!element||!element->doc||element->tag()!=MARKUP_NAME_IMG||!name) return false;
    DomImageSource* source=image_source_for(element,true);if(!source) return false;
    source->surface={};if(element->embed) element->embed->img=nullptr;
    auto* ui=(UiContext*)element->doc->js.host_ui_context;if(!ui) return false;
    // the existing loader resolves against its UI document; detached images still belong to this document.
    DomDocument* previous=ui->document;ui->document=element->doc;
    ImageSurface* image=load_document_image(element->doc,ui,name);ui->document=lam::up(previous);
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
