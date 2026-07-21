#pragma once

#include "network_resource_manager.h"

struct DomDocument;
struct DomElement;
struct CssFontFaceDescriptor;

typedef struct NetworkResourceProcessor {
    void (*process_css)(NetworkResource*, DomDocument*);
    void (*process_image)(NetworkResource*, DomElement*);
    void (*process_font)(NetworkResource*, const CssFontFaceDescriptor*);
    void (*process_svg)(NetworkResource*, DomElement*);
    void (*process_html)(NetworkResource*, DomDocument*);
    void (*process_script)(NetworkResource*, DomDocument*);
    void (*handle_failure)(NetworkResource*, DomDocument*);
    void (*release_image)(NetworkResource*);
    void (*request_layout_update)(NetworkResourceManager*, bool needs_reflow, bool needs_repaint);
} NetworkResourceProcessor;

void network_resource_processor_register(const NetworkResourceProcessor* processor);
const NetworkResourceProcessor* network_resource_processor_get();
