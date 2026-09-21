// resource_loaders.h
// Type-specific processing for loaded network resources

#ifndef RESOURCE_LOADERS_H
#define RESOURCE_LOADERS_H

#include "../lambda/network/network_resource_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations
struct DomDocument;
struct DomElement;
struct CssStylesheet;
struct CssFontFaceDescriptor;
struct CssValue;

typedef void (*RadiantCssUrlVisitor)(struct CssValue* value, void* context);

// Resource processing functions
void process_css_resource(NetworkResource* res, struct DomDocument* doc);
void process_image_resource(NetworkResource* res, struct DomElement* img_element);
void process_font_resource(NetworkResource* res, const struct CssFontFaceDescriptor* font_face);
void process_svg_resource(NetworkResource* res, struct DomElement* use_element);
void process_html_resource(NetworkResource* res, struct DomDocument* doc);
void process_script_resource(NetworkResource* res, struct DomDocument* doc);

// Normalize CSS url() values and expose them to loader-stage prefetch policy.
void radiant_resolve_stylesheet_resource_urls(struct CssStylesheet* sheet);
void radiant_for_each_stylesheet_resource_url(struct CssStylesheet* sheet,
                                              RadiantCssUrlVisitor visitor,
                                              void* context);

// Error handling
void handle_resource_failure(NetworkResource* res, struct DomDocument* doc);

#ifdef __cplusplus
}
#endif

#endif // RESOURCE_LOADERS_H
