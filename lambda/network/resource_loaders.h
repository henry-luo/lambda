// resource_loaders.h
// Type-specific processing for loaded network resources

#ifndef RESOURCE_LOADERS_H
#define RESOURCE_LOADERS_H

#include "network_resource_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations
struct DomDocument;
struct DomElement;
struct CssStylesheet;
struct CssFontFaceDescriptor;

// Resource processing functions
void process_css_resource(NetworkResource* res, struct DomDocument* doc);
void process_image_resource(NetworkResource* res, struct DomElement* img_element);
void process_font_resource(NetworkResource* res, struct CssFontFaceDescriptor* font_face);
void process_svg_resource(NetworkResource* res, struct DomElement* use_element);
void process_html_resource(NetworkResource* res, struct DomDocument* doc);

// Error handling
void handle_resource_failure(NetworkResource* res, struct DomDocument* doc);

#ifdef __cplusplus
}
#endif

#endif // RESOURCE_LOADERS_H
