#pragma once
#include <stddef.h>
struct UiContext;
struct DomDocument;
struct DomElement;
struct Pool;
struct SelectorMatcher;
bool lambda_event_sim_evaluate(UiContext* ui, const char* source, bool assertion);
bool lambda_event_sim_collect_gc(UiContext* ui, double count);
bool lambda_event_sim_animation_frame(UiContext* ui, double time, int* callbacks);
bool lambda_event_sim_measure_frames(UiContext* ui, const char* callback_name, const char* sample_name,
                                    size_t frames, double* durations, double* sampling);

double lambda_event_sim_clock_ms(bool relative);
void lambda_event_sim_dialog_response(const char* value);
SelectorMatcher* lambda_event_sim_selector_matcher(DomDocument* document, Pool* pool);
DomElement* lambda_event_sim_element_from_point(DomDocument* document, float x, float y);
