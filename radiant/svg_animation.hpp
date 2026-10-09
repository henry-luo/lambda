#pragma once

#include <stdint.h>

struct DomDocument;
struct DomElement;
struct Element;
struct UiContext;
struct ImageSurface;
struct SvgAnimationRegistry;

double svg_animation_clock_value(const char* value, double fallback);
double svg_animation_wallclock_value(const char* value, double origin);
DomElement* svg_animation_target_element(DomElement* animation);
void svg_animation_use_event(DomElement* host, DomElement* source, const char* type, bool bubbles, double detail);
void svg_animation_forget_source_document(DomDocument* host, DomDocument* source);
DomElement* svg_animation_use_source(DomElement* host);
bool svg_animation_start_time(DomElement* animation, double* seconds);
double svg_animation_simple_duration(DomElement* animation);
void svg_animation_prepare(DomElement* element);
void svg_animation_mark_reference(DomElement* root);
void svg_animation_prepare_instance(DomElement* host, DomElement* source_root);
bool svg_animation_has_elements(DomElement* element);
void svg_image_animation_register(UiContext* ui, ImageSurface* image);
void svg_animation_pause(DomElement* element, bool paused);
bool svg_animation_paused(DomElement* element);
void svg_animation_set_time(DomElement* element, double seconds);
void svg_animation_set_document_time(DomDocument* document, double seconds);
double svg_animation_current_time(DomElement* element);
bool svg_animation_begin_end(DomElement* element, bool end, double offset);
uint64_t svg_animation_generation(DomDocument* document);
bool svg_animation_is_sampling(DomDocument* document);
uint64_t svg_animation_source_generation(DomDocument* document, Element* root, DomElement* node = nullptr);
const char* svg_animation_value(DomElement* element, const char* name, bool css_only = false);
const char* svg_animation_attribute(DomElement* element, const char* name);
const char* svg_animation_source_value(Element* element, const char* name);

// source readers borrow the current document only during a synchronous SVG geometry/paint walk.
struct SvgAnimationSourceScope {
    DomDocument* previous;
    DomElement* previous_instance;
    SvgAnimationRegistry* previous_registry;
    explicit SvgAnimationSourceScope(DomDocument* document, DomElement* instance = nullptr, DomElement* host = nullptr);
    ~SvgAnimationSourceScope();
};
