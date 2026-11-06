#include "speech_event_adapter.hpp"
#include "../../../radiant/speech_event_host.hpp"
#include "../../../radiant/event.hpp"
#include "../../input/css/dom_element.hpp"
#include "../../dom/dom_events.h"
#include "../../dom/realm/dom_realm.h"
#include "../../js/js_runtime.h"
#include "../../runtime/lambda-root-frame.hpp"
#include <string.h>

struct SpeechEventData {
    const char* type;
    uint64_t token;
    const SpeechEventFields* fields;
};
static void speech_event_set(Item event, const char* key, Item value) {
    RootFrame roots(1);
    Rooted<Item> rooted(roots, value);
    dom_realm_set_name(event, key, rooted.get());
}
// fields use the snake_case names Lambda handlers read (evt.char_index); the
// LambdaJS SpeechSynthesisEvent face is M5.
static Item speech_build_event(void* userdata) {
    auto* data = (SpeechEventData*)userdata;
    const SpeechEventFields& f = *data->fields;
    RootFrame roots(1);
    Rooted<Item> event(roots, js_create_event(data->type, true, false));
    speech_event_set(event.get(), "token", data->token ? js_make_number((double)data->token) : ItemNull);
    if (!strcmp(data->type, "speechboundary")) {
        speech_event_set(event.get(), "kind", make_string_item(f.kind ? f.kind : "word"));
        speech_event_set(event.get(), "char_index", js_make_number((double)f.char_index));
        speech_event_set(event.get(), "char_length", js_make_number((double)f.char_length));
    }
    if (!strcmp(data->type, "speechmark")) speech_event_set(event.get(), "name", make_string_item(f.name ? f.name : ""));
    if (!strcmp(data->type, "speechboundary") || !strcmp(data->type, "speechmark") || !strcmp(data->type, "speechend"))
        speech_event_set(event.get(), "time", js_make_number(f.time));
    if (!strcmp(data->type, "speechend")) speech_event_set(event.get(), "cancelled", (Item){.item = b2it(f.cancelled)});
    if (!strcmp(data->type, "speechstart")) speech_event_set(event.get(), "delay", js_make_number(f.delay));
    if (!strcmp(data->type, "speecherror")) speech_event_set(event.get(), "reason", make_string_item(f.reason ? f.reason : "speech failed"));
    return event.get();
}
bool lambda_radiant_speech_event(DomElement* target, uint64_t token, const char* type,
                                  const SpeechEventFields* fields) {
    if (!target || !target->doc || !fields) return false;
    SpeechEventData data = {type, token, fields};
    return radiant_queue_timing_event((UiContext*)target->doc->js.host_ui_context,
                                      target, speech_build_event, &data, type);
}
