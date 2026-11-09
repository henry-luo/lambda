#include "scene3d_physics_event_adapter.hpp"
#include "radiant/event.hpp"
#include "lambda/input/css/dom_element.hpp"
#include "../../dom/dom_events.h"
#include "../../dom/realm/dom_realm.h"
#include "../../js/js_runtime.h"
#include "../../runtime/lambda-physics.hpp"
#include "../../runtime/lambda-root-frame.hpp"

struct PhysicsDomEvent {
    const PhysicsScene* scene;
    const PhysicsEvent* event;
    const char* type;
};

static Item physics_scene_event(void* data) {
    auto* source = (PhysicsDomEvent*)data;
    RootFrame roots(2);
    Rooted<Item> event(roots, js_create_event(source->type, true, false));
    Rooted<Item> detail(roots, physics_event_value(*source->scene, *source->event));
    dom_realm_set_name(event.get(), "detail", detail.get());
    return event.get();
}

bool lambda_scene3d_physics_event(DomElement* target, const PhysicsScene* scene,
                                  const PhysicsEvent* event, const char* type) {
    if (!target || !target->doc || !scene || !event) return false;
    PhysicsDomEvent data = {scene, event, type};
    return radiant_queue_timing_event((UiContext*)target->doc->js.host_ui_context,
                                     target, physics_scene_event, &data, type);
}
