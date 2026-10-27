// IME15/D7.2.5: this host never creates a DOM document or a UI-mode Input.
// The shared Lambda value/Mark paths retain DOM seams; fail closed if reached.
#include "../input/css/dom_element.hpp"
#include "../input/css/dom_lifecycle.hpp"
#include "../../lib/log.h"
#include "../../lib/hashmap.h"
#include "../../lib/str.h"

#ifdef LAMBDA_MACOS_IME

struct Runtime;
extern "C" void dom_set_runtime_cleanup_hook(void (*)(Runtime*)) {}

bool dom_is_comment_tag(const char* tag_name) {
    return tag_name && (strcmp(tag_name, "!--") == 0 ||
        strcmp(tag_name, "#comment") == 0 || str_ieq_cstr(tag_name, "!DOCTYPE"));
}

bool dom_node_registry_owns(DomDocument*, DomNode*) { return false; }
bool dom_node_registry_register(DomDocument*, DomNode*, size_t, bool) { return false; }
void dom_node_registry_refresh_backing(DomDocument*, DomNode*) {}
bool dom_document_owns_node_storage(DomDocument*, const void*) { return false; }
uint32_t dom_document_alloc_node_id(DomDocument*) { return 0; }
DomElement* element_dom_map_lookup(HashMap*, Element*) { return nullptr; }
void element_dom_map_insert(HashMap*, Element*, DomElement*) {}

DomElement* DomElement::create(DomDocument*, const char*, Element*) {
    log_error("macos-ime-dom: unexpected UI element creation");
    return nullptr;
}
DomElement* DomElement::create_in(Arena*) {
    log_error("macos-ime-dom: unexpected UI element allocation");
    return nullptr;
}
DomElement* DomElement::create_in(DomElement*, DomDocument*, const char*, Element*) {
    log_error("macos-ime-dom: unexpected UI element adoption");
    return nullptr;
}
DomText* DomText::create(String*, DomElement*) {
    log_error("macos-ime-dom: unexpected UI text creation");
    return nullptr;
}
DomText* DomText::create_in(Arena*, size_t) {
    log_error("macos-ime-dom: unexpected UI text allocation");
    return nullptr;
}
DomComment* DomComment::create_detached(Element*, DomDocument*) {
    log_error("macos-ime-dom: unexpected UI comment creation");
    return nullptr;
}

#endif
