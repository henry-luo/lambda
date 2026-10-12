#pragma once
#include <stdint.h>

struct EventSimContext;
struct SimEvent;
struct UiContext;
class MapReader;
struct ImageSurface;
struct DomDocument;
struct DomNode;

bool event_sim_render_recognizes(const char* name);
bool event_sim_render_parse(MapReader& arguments, SimEvent* event);
bool event_sim_render_execute(EventSimContext* context, UiContext* ui, SimEvent* event);
void event_sim_render_event_free(SimEvent* event);
void event_sim_render_free(EventSimContext* context);
void event_sim_render_surface(UiContext* ui);
DomNode* event_sim_find_element(DomDocument* document, const char* selector,
                                int index = 0, bool flush_reflow = true);
bool event_sim_pixel_matches_bounds(uint32_t pixel, const SimEvent* event, bool report_error);
bool event_sim_create_parent_dirs(const char* path);
bool event_sim_path_is_temp_scratch(const char* path);
