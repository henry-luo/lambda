#include "svg_animation.hpp"
#include "animation_value.hpp"
#include "render.hpp"
#include "event.hpp"
#include "../lambda/dom/dom.h"
#include "../lambda/input/css/css_engine.hpp"
#include "../lambda/input/css/dom_lifecycle.hpp"
#include "../lib/hashmap_typed.hpp"
#include "../lib/arraylist.hpp"
#include "../lib/mem_factory.h"
#include "../lib/str.h"
#include "../lib/datetime.h"
#include <math.h>
#include <float.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct SvgAnimatedValue {
    const char* name;
    const char* text;
    const char* resolved_text;
    bool css;
    bool resolved;
    SvgAnimatedValue* next;
};

struct SvgAnimatedTarget {
    Element* source;
    DomNodeRef element;
    SvgAnimatedValue* values;
};

typedef TypedHashMap<SvgAnimatedTarget,
    HashMapPointerMemberKeyOps<SvgAnimatedTarget, &SvgAnimatedTarget::source>> SvgAnimatedTargetMap;

struct SvgAnimationInstanceTime {
    double value;
    double resolved_at;
    bool end;
    bool broadcast;
    SvgAnimationInstanceTime* next;
};

struct SvgAnimationControl {
    DomElement* address;
    DomNodeRef element;
    SvgAnimationInstanceTime* times;
    unsigned count;
    double notified_begin;
    double notified_end;
    double notified_repeat;
    bool begin_notified;
    bool end_notified;
    char* frozen_text;
    DomNodeRef frozen_target;
    uint64_t frozen_signature;
    double frozen_begin;
    double frozen_end;
};

typedef TypedHashMap<SvgAnimationControl,
    HashMapPointerMemberKeyOps<SvgAnimationControl, &SvgAnimationControl::address>> SvgAnimationControlMap;

struct SvgAnimationRegistry;
struct SvgAnimationUseInstance {
    DomNodeRef host;
    DomNodeRef source;
    SvgAnimationRegistry* registry;
    SvgAnimationUseInstance* next;
};
struct SvgTimeline {
    SvgAnimationRegistry* registry;
    DomNodeRef root;
    AnimationInstance* driver;
    double time;
    double origin;
    double wallclock_origin;
    bool paused;
    bool ticking;
    bool layout_affects;
    SvgTimeline* next;
};

struct SvgAnimationRegistry : DomDocumentResourceData {
    DomDocument* document;
    DomDocument* owner_document;
    SvgAnimationRegistry* budget_owner;
    SvgAnimationRegistry* instance_parent;
    SvgAnimationUseInstance* instances;
    DomNodeRef instance_root;
    DomNodeRef instance_host;
    uint64_t original_generation;
    MemNode* memory_node;
    SvgTimeline* timelines;
    Pool* samples;
    HashMap* targets;
    HashMap* controls;
    uint64_t generation;
    uint64_t sampled_generation;
    uint64_t sampled_epoch;
    bool sampling;
    bool reference_document;
    size_t sample_bytes;
    bool sample_failed;
    size_t frozen_bytes;
    size_t instance_count;
    size_t total_sample_bytes;
    size_t total_frozen_bytes;
    size_t total_instance_count;
    size_t total_control_count;
    size_t total_use_count;
    size_t total_timeline_count;
    unsigned frozen_depth;
    double event_time_offset;
};

// one timing walk shares its limit across every animation and nested frozen sandwich.
struct SvgAnimationWorkScope;
static thread_local SvgAnimationWorkScope* svg_animation_work = nullptr;
struct SvgAnimationWorkScope {
    SvgAnimationRegistry* registry;
    SvgAnimationWorkScope* previous;
    size_t remaining = 1048576;
    bool reported = false;
    explicit SvgAnimationWorkScope(SvgAnimationRegistry* owner)
        : registry(owner->budget_owner), previous(svg_animation_work) {
        if (!previous || previous->registry != registry) svg_animation_work = this;
    }
    ~SvgAnimationWorkScope() { svg_animation_work = previous; }
};

static void svg_animation_notify_time(SvgTimeline* timeline, double previous, unsigned depth = 0);

static MemContext* svg_animation_memory(DomDocument* document) {
    return document->services.mem_ctx ? (MemContext*)document->services.mem_ctx : mem_context_root();
}

static void* svg_animation_alloc(SvgAnimationRegistry* registry, size_t bytes) {
    const size_t budget = 64u * 1024u * 1024u;
    if (registry->sample_failed || bytes > budget - registry->budget_owner->total_sample_bytes) {
        if (!registry->sample_failed) log_error("SVG_ANIMATION_LIMIT: sample storage exceeds %zu bytes", budget);
        registry->sample_failed = true;
        return nullptr;
    }
    void* result = pool_calloc(registry->samples, bytes);
    if (!result) registry->sample_failed = true;
    else { registry->sample_bytes += bytes; registry->budget_owner->total_sample_bytes += bytes; }
    return result;
}

static const char* svg_animation_copy(SvgAnimationRegistry* registry, const char* text) {
    if (!text) return nullptr;
    size_t length = strlen(text);
    char* result = (char*)svg_animation_alloc(registry, length + 1);
    if (result) memcpy(result, text, length);
    return result;
}

static bool svg_animation_keyword(const char* text, const char* keyword) {
    if (!text) return false;
    size_t length = strlen(text);
    str_trim(&text, &length);
    return length == strlen(keyword) && strncmp(text, keyword, length) == 0;
}

static bool svg_animation_name_in(const char* names, const char* name) {
    StrSplitIter split;
    str_split_byte_init(&split, names, strlen(names), ' ');
    const char* token; size_t length;
    while (str_split_next(&split, &token, &length))
        if (strlen(name) == length && strncmp(token, name, length) == 0) return true;
    return false;
}

static const char* svg_animation_token(SvgAnimationRegistry* registry, const char* text) {
    if (!text) return nullptr;
    size_t length = strlen(text);
    if (length > 65536) return nullptr;
    str_trim(&text, &length);
    char* token = (char*)svg_animation_alloc(registry, length + 1);
    if (token) memcpy(token, text, length);
    return token;
}

static bool svg_animation_filter_primitive(const char* tag);

static thread_local DomDocument* svg_animation_source_document = nullptr;
static thread_local DomElement* svg_animation_source_instance = nullptr;
static thread_local SvgAnimationRegistry* svg_animation_instance_registry = nullptr;
static thread_local bool svg_animation_event_broadcast = true;
static SvgAnimationRegistry* svg_animation_use_registry(DomElement* host, DomElement* source);

SvgAnimationSourceScope::SvgAnimationSourceScope(DomDocument* document, DomElement* instance, DomElement* host)
    : previous(svg_animation_source_document), previous_instance(svg_animation_source_instance),
      previous_registry(svg_animation_instance_registry) {
    svg_animation_source_document = document;
    svg_animation_source_instance = instance ? instance : document == previous ? previous_instance : nullptr;
    svg_animation_instance_registry = host && instance ? svg_animation_use_registry(host, instance) :
        document == previous ? previous_registry : nullptr;
}
SvgAnimationSourceScope::~SvgAnimationSourceScope() {
    svg_animation_source_document = previous; svg_animation_source_instance = previous_instance;
    svg_animation_instance_registry = previous_registry;
}

double svg_animation_clock_value(const char* value, double fallback) {
    if (!value) return fallback;
    value = str_skip_ascii_space(value);
    if (strncmp(value, "indefinite", 10) == 0 && !*str_skip_ascii_space(value + 10)) return INFINITY;
    double sign = 1.0;
    if (*value == '-' || *value == '+') { if (*value == '-') sign = -1.0; value = str_skip_ascii_space(value + 1); }
    const char* digits = value;
    while (*digits >= '0' && *digits <= '9') digits++;
    if (digits == value) return fallback;
    const char* finish = digits;
    if (*finish == '.') {
        finish++; const char* fraction = finish;
        while (*finish >= '0' && *finish <= '9') finish++;
        if (finish == fraction) return fallback;
    }
    char* end = nullptr;
    double number = strtod(value, &end);
    if (end != finish || !isfinite(number)) return fallback;
    if (*finish == ':') {
        if (finish != digits) return fallback;
        double first = number;
        const char* next = end + 1;
        if (next[0] < '0' || next[0] > '9' || next[1] < '0' || next[1] > '9') return fallback;
        number = strtod(next, &end);
        if (end == next || number < 0.0 || number >= 60.0) return fallback;
        if (*end == ':') {
            if (end != next + 2) return fallback;
            double minutes = number;
            next = end + 1;
            if (next[0] < '0' || next[0] > '9' || next[1] < '0' || next[1] > '9') return fallback;
            number = strtod(next, &end);
            if (end == next || number < 0.0 || number >= 60.0) return fallback;
            number += first * 3600.0 + minutes * 60.0;
        } else {
            if (digits != value + 2 || first >= 60.0) return fallback;
            number += first * 60.0;
        }
        const char* seconds_end = next + 2;
        if (*seconds_end == '.') {
            seconds_end++; const char* fraction = seconds_end;
            while (*seconds_end >= '0' && *seconds_end <= '9') seconds_end++;
            if (seconds_end == fraction) return fallback;
        }
        return end == seconds_end && !*str_skip_ascii_space(end) && isfinite(number) ? sign * number : fallback;
    }
    const char* unit = end;
    while (*end >= 'a' && *end <= 'z') end++;
    if (*str_skip_ascii_space(end)) return fallback;
    size_t length = end - unit;
    double factor = !length || (length == 1 && *unit == 's') ? 1.0 :
        length == 2 && strncmp(unit, "ms", 2) == 0 ? 0.001 :
        length == 3 && strncmp(unit, "min", 3) == 0 ? 60.0 :
        length == 1 && *unit == 'h' ? 3600.0 : NAN;
    double result = sign * number * factor;
    return isfinite(result) ? result : fallback;
}

static bool svg_animation_connected(DomDocument* doc, DomNodeRef ref) {
    DomNode* node = dom_node_ref_validate(doc, ref);
    for (unsigned depth = 0; node && depth < 256; depth++, node = node->parent)
        if (node == doc->root) return true;
    return false;
}

static bool svg_animation_wallclock_digits(const char** cursor, unsigned count, unsigned* value) {
    *value = 0;
    for (unsigned i = 0; i < count; i++) {
        unsigned char digit = (unsigned char)**cursor;
        if (digit < '0' || digit > '9') return false;
        *value = *value * 10 + digit - '0'; (*cursor)++;
    }
    return true;
}

double svg_animation_wallclock_value(const char* value, double origin) {
    if (!value || !isfinite(origin) || strncmp(value, "wallclock(", 10) != 0) return NAN;
    const char* cursor = str_skip_ascii_space(value + 10);
    time_t origin_seconds = (time_t)floor(origin);
    struct tm local = {};
#ifdef _WIN32
    if (localtime_s(&local, &origin_seconds)) return NAN;
#else
    if (!localtime_r(&origin_seconds, &local)) return NAN;
#endif
    unsigned year = (unsigned)(local.tm_year + 1900), month = (unsigned)local.tm_mon + 1;
    unsigned day = (unsigned)local.tm_mday, hour = 0, minute = 0, second = 0;
    bool has_date = strlen(cursor) >= 5 && cursor[4] == '-';
    if (has_date) {
        if (!svg_animation_wallclock_digits(&cursor, 4, &year) || *cursor++ != '-' ||
            !svg_animation_wallclock_digits(&cursor, 2, &month) || *cursor++ != '-' ||
            !svg_animation_wallclock_digits(&cursor, 2, &day) || month < 1 || month > 12 || day < 1) return NAN;
        int64_t days = datetime_days_from_civil(year + (month == 12), month % 12 + 1, 1) -
            datetime_days_from_civil(year, month, 1);
        if (day > days) return NAN;
    }
    double fraction = 0;
    bool has_time = !has_date || *cursor == 'T';
    if (has_time) {
        if (has_date) cursor++;
        if (!svg_animation_wallclock_digits(&cursor, 2, &hour) || *cursor++ != ':' ||
            !svg_animation_wallclock_digits(&cursor, 2, &minute) || hour > 23 || minute > 59) return NAN;
        if (*cursor == ':') {
            cursor++;
            if (!svg_animation_wallclock_digits(&cursor, 2, &second) || second > 59) return NAN;
            if (*cursor == '.') {
                cursor++; const char* start = cursor; double place = .1;
                while (*cursor >= '0' && *cursor <= '9') {
                    fraction += (*cursor++ - '0') * place; place *= .1;
                }
                if (cursor == start) return NAN;
            }
        }
    }
    bool zoned = has_time && (*cursor == 'Z' || *cursor == '+' || *cursor == '-');
    double zone = 0;
    if (zoned) {
        char sign = *cursor++;
        if (sign != 'Z') {
            unsigned hours, minutes;
            if (!svg_animation_wallclock_digits(&cursor, 2, &hours) || *cursor++ != ':' ||
                !svg_animation_wallclock_digits(&cursor, 2, &minutes) || hours > 23 || minutes > 59) return NAN;
            zone = (hours * 3600.0 + minutes * 60.0) * (sign == '-' ? -1 : 1);
        }
    }
    cursor = str_skip_ascii_space(cursor);
    if (*cursor++ != ')' || *str_skip_ascii_space(cursor)) return NAN;
    double epoch;
    if (zoned) epoch = datetime_days_from_civil(year, month, day) * 86400.0 +
        hour * 3600.0 + minute * 60.0 + second - zone;
    else {
        // SMIL wallclock values without a zone use the presentation location, including DST.
        local.tm_year = (int)year - 1900; // INT_CAST_OK: calendar field, not layout geometry.
        local.tm_mon = (int)month - 1; // INT_CAST_OK: calendar field, not layout geometry.
        local.tm_mday = (int)day; // INT_CAST_OK: calendar field, not layout geometry.
        local.tm_hour = (int)hour; // INT_CAST_OK: calendar field, not layout geometry.
        local.tm_min = (int)minute; // INT_CAST_OK: calendar field, not layout geometry.
        local.tm_sec = (int)second; // INT_CAST_OK: calendar field, not layout geometry.
        local.tm_isdst = -1;
        epoch = (double)mktime(&local);
    }
    return epoch + fraction - origin;
}

static DomElement* svg_animation_root(DomElement* element) {
    DomElement* root = nullptr;
    for (DomNode* node = element; node && node->is_element(); node = node->parent) {
        // SVG 2 §5.1.3: an HTML integration point ends the current SVG fragment.
        if (!dom_element_is_svg(node->as_element())) break;
        if (node->as_element()->tag() == MARKUP_NAME_SVG) root = node->as_element();
    }
    return root;
}

static bool svg_animation_separate_fragment(DomElement* element, DomNode* root) {
    return element != root && element->tag() == MARKUP_NAME_SVG && svg_animation_root(element) == element;
}

static void svg_animation_driver_released(AnimationInstance* instance) {
    SvgTimeline* timeline = (SvgTimeline*)instance->target;
    timeline->driver = nullptr;
}

static void svg_animation_note_time(SvgTimeline* timeline, double time) {
    if (timeline->time == time) return;
    timeline->time = time;
    timeline->registry->generation++;
    DomDocument* doc = timeline->registry->owner_document;
    if (doc->state) {
        if (timeline->layout_affects) {
            DomNode* root = dom_node_ref_validate(timeline->registry->document, timeline->root);
            if (root) dom_invalidate_layout_subtree(root);
            doc_state_request_reflow(doc->state);
        }
        doc_state_request_repaint(doc->state);
    }
}

static void svg_animation_tick(AnimationInstance* instance, float) {
    SvgTimeline* timeline = (SvgTimeline*)instance->target;
    DomDocument* doc = timeline->registry->document;
    if (!svg_animation_connected(doc, timeline->root)) {
        instance->play_state = ANIM_PLAY_FINISHED;
        return;
    }
    double previous = timeline->time;
    svg_animation_note_time(timeline, animation_clock_time(doc->state->animation_scheduler, instance, timeline->origin));
    timeline->ticking = true;
    svg_animation_notify_time(timeline, previous);
    timeline->ticking = false;
    if (timeline->paused || !svg_animation_connected(doc, timeline->root))
        instance->play_state = ANIM_PLAY_FINISHED;
}

static void svg_animation_start_driver(SvgTimeline* timeline, unsigned depth = 0) {
    if (depth >= 32) return;
    if (timeline->paused || timeline->driver) return;
    DomDocument* doc = timeline->registry->document;
    if (timeline->registry->instance_parent) {
        SvgAnimationRegistry* parent = timeline->registry->instance_parent;
        DomNode* host = dom_node_ref_validate(parent->document, timeline->registry->instance_host);
        DomElement* root = host && host->is_element() ? svg_animation_root(host->as_element()) : nullptr;
        // nested external sources start the outer host driver, while keeping their own clocks private.
        for (SvgTimeline* clock = parent->timelines; root && clock; clock = clock->next)
            if (clock->root.address == root && clock->root.expected_id == root->DomNode::id)
                svg_animation_start_driver(clock, depth + 1);
        return;
    }
    // pinned detached DOM can request preparation without becoming a live scheduling root.
    if (!svg_animation_connected(doc, timeline->root)) return;
    if (!doc->state && !state_store_create(doc)) return;
    AnimationScheduler* scheduler = doc->state->animation_scheduler;
    timeline->origin = timeline->time;
    timeline->driver = animation_clock_driver_start(scheduler, ANIM_SVG, timeline,
        svg_animation_tick, svg_animation_driver_released);
}

static void svg_animation_clear_frozen(SvgAnimationRegistry* registry, SvgAnimationControl* control) {
    if (!control || !control->frozen_text) return;
    registry->frozen_bytes -= strlen(control->frozen_text) + 1;
    registry->budget_owner->total_frozen_bytes -= strlen(control->frozen_text) + 1;
    lam::Temp<char> dropped(control->frozen_text); control->frozen_text = nullptr;
}

static bool svg_animation_control_destroy(const void* data, void* context) {
    const SvgAnimationControl* control = (const SvgAnimationControl*)data;
    SvgAnimationRegistry* registry = (SvgAnimationRegistry*)context;
    registry->instance_count -= control->count;
    registry->budget_owner->total_instance_count -= control->count;
    svg_animation_clear_frozen(registry, (SvgAnimationControl*)control);
    for (SvgAnimationInstanceTime* time = control->times; time;) {
        SvgAnimationInstanceTime* next = time->next; lam::Temp<SvgAnimationInstanceTime> owned(time); time = next;
    }
    return true;
}

static void svg_animation_prune(SvgAnimationRegistry* registry) {
    if (registry->sampling) return;
    lam::ArrayList<SvgAnimationControl> retired(MEM_CAT_RENDER, 0);
    size_t cursor = 0; SvgAnimationControl* control = nullptr;
    while (SvgAnimationControlMap::next(registry->controls, &cursor, &control)) {
        if (!dom_node_ref_validate(registry->document, control->element)) {
            SvgAnimationControl key = {}; key.address = control->address;
            if (!retired.append(key)) break;
        }
    }
    // deletion can move hash entries, so collect keys before releasing their owned state.
    for (const SvgAnimationControl& key : retired) {
        control = SvgAnimationControlMap::get(registry->controls, key);
        if (control) svg_animation_control_destroy(control, registry);
        registry->budget_owner->total_control_count--;
        SvgAnimationControlMap::erase(registry->controls, key);
    }
    for (SvgTimeline** link = &registry->timelines; *link;) {
        SvgTimeline* timeline = *link;
        if (!timeline->driver && !dom_node_ref_validate(registry->document, timeline->root)) {
            *link = timeline->next; registry->budget_owner->total_timeline_count--;
            lam::Temp<SvgTimeline> owned(timeline);
        } else link = &timeline->next;
    }
}

static bool svg_animation_control_stat(const void* data, void* context) {
    const SvgAnimationControl* control = (const SvgAnimationControl*)data;
    MemStatSample* sample = (MemStatSample*)context;
    sample->bytes_in_use += sizeof(*control) + control->count * sizeof(SvgAnimationInstanceTime);
    sample->alloc_count += 1 + control->count;
    return true;
}

static bool svg_animation_registry_stat(void* data, MemStatSample* sample) {
    SvgAnimationRegistry* registry = (SvgAnimationRegistry*)data;
    // the sample pool has its own coordinator node; count only separately owned semantic state here.
    sample->bytes_in_use = sizeof(*registry) + registry->frozen_bytes +
        SvgAnimatedTargetMap::count(registry->targets) * sizeof(SvgAnimatedTarget);
    sample->alloc_count = 1 + SvgAnimatedTargetMap::count(registry->targets);
    hashmap_scan(registry->controls, svg_animation_control_stat, sample);
    for (SvgTimeline* timeline = registry->timelines; timeline; timeline = timeline->next) {
        sample->bytes_in_use += sizeof(*timeline); sample->alloc_count++;
    }
    for (SvgAnimationUseInstance* instance = registry->instances; instance; instance = instance->next) {
        sample->bytes_in_use += sizeof(*instance); sample->alloc_count++;
    }
    sample->bytes_reserved = sample->bytes_in_use;
    return true;
}

static void svg_animation_registry_destroy(DomDocumentResourceData* data) {
    SvgAnimationRegistry* registry = (SvgAnimationRegistry*)data;
    // the destroy call hands the registry over
    lam::Temp<SvgAnimationRegistry> owned(registry);
    mem_unregister(registry->memory_node);
    for (SvgAnimationUseInstance* instance = registry->instances; instance;) {
        SvgAnimationUseInstance* next = instance->next;
        registry->budget_owner->total_use_count--;
        svg_animation_registry_destroy(instance->registry); lam::Temp<SvgAnimationUseInstance> owned(instance); instance = next;
    }
    // D4.2.6/D4.5.1v3: cancel callbacks stop borrowing the document before sample storage is released.
    for (SvgTimeline* timeline = registry->timelines; timeline;) {
        SvgTimeline* next = timeline->next;
        if (timeline->driver && registry->document->state)
            animation_scheduler_cancel(registry->document->state->animation_scheduler, timeline->driver);
        registry->budget_owner->total_timeline_count--;
        lam::Temp<SvgTimeline> owned(timeline); timeline = next;
    }
    SvgAnimatedTargetMap::destroy(registry->targets);
    hashmap_scan(registry->controls, svg_animation_control_destroy, registry);
    registry->budget_owner->total_control_count -= SvgAnimationControlMap::count(registry->controls);
    registry->budget_owner->total_sample_bytes -= registry->sample_bytes;
    SvgAnimationControlMap::destroy(registry->controls);
    if (registry->samples) mem_pool_destroy(registry->samples);
    if (!registry->instance_host.address) registry->document->services.svg_animation_registry = nullptr;
}

static SvgAnimationRegistry* svg_animation_registry_new(DomDocument* doc, DomDocument* owner, bool published) {
    SvgAnimationRegistry* registry = (SvgAnimationRegistry*)mem_calloc(1, sizeof(*registry), MEM_CAT_RENDER); // OBJ_HEAP_OK: the owner document's resource hook, or the parent registry's use instance, owns it; svg_animation_registry_destroy releases it
    if (!registry) return nullptr;
    registry->document = doc; registry->owner_document = owner; registry->generation = 1;
    registry->budget_owner = registry;
    registry->targets = SvgAnimatedTargetMap::create(32);
    registry->controls = SvgAnimationControlMap::create(32);
    registry->samples = mem_pool_create((MemContext*)owner->services.mem_ctx,
        MEM_ROLE_RENDER, "svg.animation.samples");
    if (registry->targets && registry->controls && registry->samples)
        registry->memory_node = mem_register(svg_animation_memory(owner),
            MEM_KIND_CACHE, MEM_ROLE_RENDER, "svg.animation.state", registry, nullptr,
            svg_animation_registry_stat, nullptr);
    if (!registry->targets || !registry->controls || !registry->samples ||
        (published && !dom_document_add_resource(owner, registry, svg_animation_registry_destroy))) {
        mem_unregister(registry->memory_node);
        SvgAnimatedTargetMap::destroy(registry->targets);
        SvgAnimationControlMap::destroy(registry->controls);
        if (registry->samples) mem_pool_destroy(registry->samples);
        lam::Temp<SvgAnimationRegistry> dropped(registry); return nullptr;
    }
    return registry;
}

static SvgAnimationRegistry* svg_animation_registry(DomDocument* doc, bool create) {
    if (!doc) return nullptr;
    if (svg_animation_instance_registry && svg_animation_instance_registry->document == doc)
        return svg_animation_instance_registry;
    SvgAnimationRegistry* registry = (SvgAnimationRegistry*)doc->services.svg_animation_registry;
    if (!registry && create) {
        registry = svg_animation_registry_new(doc, doc, true);
        doc->services.svg_animation_registry = registry;
    }
    return registry;
}

static SvgAnimationControl* svg_animation_control(SvgAnimationRegistry* registry,
    DomElement* element, bool create) {
    SvgAnimationControl key = {}; key.address = element;
    SvgAnimationControl* control = SvgAnimationControlMap::get(registry->controls, key);
    if (control && !dom_node_ref_validate(registry->document, control->element)) {
        // an address recycled for a new DOM generation does not inherit old instance times.
        svg_animation_control_destroy(control, registry); control->times = nullptr; control->count = 0;
        control->element = dom_node_ref(element);
        control->begin_notified = control->end_notified = false; control->notified_repeat = 0;
    }
    if (!control && create) {
        if (registry->budget_owner->total_control_count >= 65536) {
            log_error("SVG_ANIMATION_LIMIT: document control storage exceeds 65536 elements");
            return nullptr;
        }
        key.element = dom_node_ref(element);
        SvgAnimationControlMap::set(registry->controls, key);
        control = SvgAnimationControlMap::get(registry->controls, key);
        if (control) registry->budget_owner->total_control_count++;
    }
    return control;
}

static SvgTimeline* svg_animation_timeline(DomElement* element, bool create) {
    DomElement* root = svg_animation_root(element);
    if (!root) return nullptr;
    SvgAnimationRegistry* registry = svg_animation_registry(root->doc, create);
    if (!registry) return nullptr;
    for (SvgTimeline* timeline = registry->timelines; timeline; timeline = timeline->next)
        if (timeline->root.address == root && timeline->root.expected_id == root->DomNode::id) return timeline;
    if (!create) return nullptr;
    if (registry->budget_owner->total_timeline_count >= 4096) { log_error("SVG_ANIMATION_LIMIT: document exceeds 4096 timelines"); return nullptr; }
    SvgTimeline* timeline = (SvgTimeline*)mem_calloc(1, sizeof(*timeline), MEM_CAT_RENDER); // OBJ_HEAP_OK: the registry's timeline list owns it; prune and registry teardown release it
    if (!timeline) return nullptr;
    timeline->registry = registry; timeline->root = dom_node_ref(root);
    struct timespec wallclock;
    clock_gettime(CLOCK_REALTIME, &wallclock);
    timeline->wallclock_origin = (double)wallclock.tv_sec + (double)wallclock.tv_nsec * 1e-9;
    timeline->next = registry->timelines; registry->timelines = timeline;
    registry->budget_owner->total_timeline_count++;
    registry->generation++;
    return timeline;
}

static bool svg_animation_is_element(DomElement* element) {
    const char* tag = element->local_name();
    return dom_element_is_svg(element) && (strcmp(tag, "animate") == 0 ||
        strcmp(tag, "animateTransform") == 0 || strcmp(tag, "set") == 0);
}

struct SvgAnimationScan {
    bool has_elements;
    bool layout_affects;
    size_t remaining;
    bool has_uses = false;
};

static void svg_animation_scan(DomElement* element, DomElement* root,
    SvgAnimationScan* scan, unsigned depth, bool all_fragments = false) {
    if (!element || depth >= 256 || !scan->remaining) return;
    if (!all_fragments && svg_animation_separate_fragment(element, root)) return;
    scan->remaining--;
    if (dom_element_is_svg(element) && strcmp(element->local_name(), "use") == 0) scan->has_uses = true;
    if (svg_animation_is_element(element)) {
        scan->has_elements = true;
        const char* name = element->get_attribute("attributeName");
        const char* href = element->get_attribute("href");
        if (!href) href = element->get_attribute("xlink:href");
        bool targets_root = element->parent == root || (href && href[0] == '#' &&
            root->get_attribute("id") && strcmp(href + 1, root->get_attribute("id")) == 0);
        if (targets_root && name && (strcmp(name, "width") == 0 || strcmp(name, "height") == 0 ||
            strcmp(name, "display") == 0)) scan->layout_affects = true;
    }
    // embedded HTML can inherit animated SVG fonts and has viewport-dependent containing blocks.
    if (element->tag() == MARKUP_NAME_FOREIGNOBJECT) scan->layout_affects = true;
    for (DomNode* node = element->first_child; node; node = node->next_sibling)
        if (node->is_element()) svg_animation_scan(node->as_element(), root, scan, depth + 1, all_fragments);
}

static void svg_animation_prepare_tree(DomElement* element, unsigned depth, size_t* remaining) {
    if (!element || depth >= 256 || !*remaining) return;
    (*remaining)--;
    if (element->tag() == MARKUP_NAME_SVG && svg_animation_root(element) == element) {
        SvgAnimationScan scan = {false, false, 65536};
        svg_animation_scan(element, element, &scan, 0);
        SvgTimeline* timeline = scan.has_elements ? svg_animation_timeline(element, true) : nullptr;
        if (timeline) timeline->layout_affects = scan.layout_affects;
        if (timeline && element->doc->js.host_ui_context && !element->doc->services.svg_image_document)
            svg_animation_start_driver(timeline);
    }
    // HTML documents can contain several independent outer SVG fragments.
    for (DomNode* child = element->first_child; child; child = child->next_sibling)
        if (child->is_element()) svg_animation_prepare_tree(child->as_element(), depth + 1, remaining);
}

void svg_animation_prepare(DomElement* element) {
    SvgAnimationRegistry* registry = element ? svg_animation_registry(element->doc, false) : nullptr;
    if (registry) svg_animation_prune(registry);
    size_t remaining = 65536;
    svg_animation_prepare_tree(element, 0, &remaining);
}

bool svg_animation_has_elements(DomElement* element) {
    SvgAnimationScan scan = {false, false, 65536};
    // picture detection includes every fragment even though each evaluator owns only one clock.
    svg_animation_scan(element, element, &scan, 0, true);
    return scan.has_elements;
}

void svg_animation_mark_reference(DomElement* root) {
    if (!root || !svg_animation_has_elements(root)) return;
    SvgAnimationRegistry* registry = svg_animation_registry(root->doc, true);
    if (registry) registry->reference_document = true;
}

void svg_animation_prepare_instance(DomElement* host, DomElement* source_root) {
    if (!host || !source_root || !svg_animation_has_elements(source_root)) return;
    SvgTimeline* timeline = svg_animation_timeline(host, true);
    if (!timeline) return;
    if (timeline->registry->owner_document->js.host_ui_context && !timeline->registry->owner_document->services.svg_image_document)
        svg_animation_start_driver(timeline);
    svg_animation_prepare(source_root);
    svg_animation_set_document_time(source_root->doc, timeline->time);
}

static SvgAnimationRegistry* svg_animation_use_registry(DomElement* host, DomElement* source) {
    if (!host || !source) return nullptr;
    SvgAnimationScan scan = {false, false, 65536};
    svg_animation_scan(source, source, &scan, 0, true);
    // an outer instance also owns the contexts of animations reached through nested use references.
    if (!scan.has_elements && !scan.has_uses) return nullptr;
    // instance samples own storage in the host; their source DOM is borrowed only during this scope.
    SvgAnimationRegistry* saved = svg_animation_instance_registry;
    SvgTimeline* host_timeline = svg_animation_timeline(host, true);
    SvgAnimationRegistry* owner = host_timeline ? host_timeline->registry : nullptr;
    svg_animation_instance_registry = saved;
    if (!owner) return nullptr;
    SvgAnimationUseInstance* found = nullptr;
    for (SvgAnimationUseInstance** link = &owner->instances; *link;) {
        SvgAnimationUseInstance* instance = *link;
        if (!svg_animation_connected(owner->document, instance->host) ||
            (instance->host.address == host && (instance->host.expected_id != host->DomNode::id ||
             instance->source.address != source || instance->source.expected_id != source->DomNode::id))) {
            *link = instance->next;
            owner->budget_owner->total_use_count--;
            svg_animation_registry_destroy(instance->registry); lam::Temp<SvgAnimationUseInstance> owned(instance);
            continue;
        }
        if (instance->host.address == host && instance->host.expected_id == host->DomNode::id &&
            instance->source.address == source && instance->source.expected_id == source->DomNode::id)
            found = instance;
        link = &instance->next;
    }
    if (!found) {
        if (owner->budget_owner->total_use_count >= 4096) { log_error("SVG_ANIMATION_LIMIT: document exceeds 4096 use instances"); return nullptr; }
        found = (SvgAnimationUseInstance*)mem_calloc(1, sizeof(*found), MEM_CAT_RENDER); // OBJ_HEAP_OK: the owner registry's use-instance list owns it
        if (!found) return nullptr;
        found->registry = svg_animation_registry_new(source->doc, owner->owner_document, false);
        if (!found->registry) { lam::Temp<SvgAnimationUseInstance> dropped(found); return nullptr; }
        found->host = dom_node_ref(host); found->source = dom_node_ref(source);
        found->registry->instance_host = found->host; found->registry->instance_root = found->source;
        found->registry->budget_owner = owner->budget_owner;
        found->registry->instance_parent = owner;
        owner->budget_owner->total_use_count++;
        found->next = owner->instances; owner->instances = found;
    }
    svg_animation_instance_registry = found->registry;
    SvgAnimationRegistry* original = (SvgAnimationRegistry*)source->doc->services.svg_animation_registry;
    uint64_t generation = original ? original->generation : 0;
    if (found->registry->original_generation != generation) {
        found->registry->original_generation = generation; found->registry->generation++;
    }
    SvgTimeline* timeline = svg_animation_timeline(source, true);
    if (timeline) {
        timeline->wallclock_origin = host_timeline->wallclock_origin;
        timeline->paused = host_timeline->paused;
        svg_animation_note_time(timeline, host_timeline->time);
    }
    svg_animation_instance_registry = saved;
    return found->registry;
}

struct SvgImageAnimation {
    DomDocument* document;
    ImageSurface* image;
    MemNode* memory_node;
};

static bool svg_image_animation_stat(void*, MemStatSample* sample) {
    sample->bytes_reserved = sample->bytes_in_use = sizeof(SvgImageAnimation);
    sample->alloc_count = 1;
    return true;
}

static void svg_image_animation_tick(AnimationInstance* instance, float) {
    SvgImageAnimation* player = (SvgImageAnimation*)instance->state;
    DomDocument* doc = player->document;
    if (!doc || !doc->state || !player->image->pic) { instance->play_state = ANIM_PLAY_FINISHED; return; }
    double seconds = fmax(0, doc->state->animation_scheduler->current_time - instance->start_time);
    if (rdt_picture_animation_time(player->image->pic) == seconds) return;
    // picture draw state is private to this surface; immutable parsed picture owners stay at time zero.
    rdt_picture_set_animation_time(player->image->pic, seconds);
    if (player->image->pixels) {
        // the surface owns its decoded raster; a new time re-decodes from the picture
        lam::free_owned(player->image->owned_pixels); player->image->pixels = nullptr;
        player->image->decoded_width = player->image->decoded_height = player->image->pitch = 0;
    }
    image_surface_bump_generation(player->image);
    doc_state_request_repaint(doc->state);
}

static void svg_image_animation_release(AnimationInstance* instance) {
    SvgImageAnimation* player = (SvgImageAnimation*)instance->state;
    if (player) { mem_unregister(player->memory_node); lam::Temp<SvgImageAnimation> owned(player); }
    instance->state = nullptr;
}

void svg_image_animation_register(UiContext* ui, ImageSurface* image) {
    DomDocument* doc = ui ? ui->document : nullptr;
    if (!doc || !image || !image->pic || !doc->js.host_ui_context || doc->services.svg_image_document ||
        !render_svg_picture_has_animation(image->pic)) return;
    if (!doc->state && !state_store_create(doc)) return;
    AnimationScheduler* scheduler = doc->state->animation_scheduler;
    for (AnimationInstance* driver = scheduler->first; driver; driver = driver->next)
        if (driver->type == ANIM_SVG && driver->target == image) return;
    SvgImageAnimation* player = (SvgImageAnimation*)mem_calloc(1, sizeof(*player), MEM_CAT_RENDER); // OBJ_HEAP_OK: the scheduler's animation instance owns the player state; svg_image_animation_release frees it
    if (!player) return;
    player->document = doc; player->image = image;
    player->memory_node = mem_register(svg_animation_memory(doc),
        MEM_KIND_CACHE, MEM_ROLE_MEDIA, "svg.image.clock", player, nullptr, svg_image_animation_stat, nullptr);
    AnimationInstance* driver = animation_instance_create(scheduler);
    if (!driver) { mem_unregister(player->memory_node); lam::Temp<SvgImageAnimation> dropped(player); return; }
    driver->type = ANIM_SVG; driver->target = image; driver->state = player;
    driver->duration = INFINITY; driver->start_time = scheduler->current_time;
    driver->tick = svg_image_animation_tick;
    driver->on_finish = driver->on_cancel = svg_image_animation_release;
    animation_scheduler_add(scheduler, driver);
}

void svg_animation_pause(DomElement* element, bool paused) {
    // SVG Animations §5.8: nested svg controls cannot change their outer fragment's clock.
    if (!element || svg_animation_root(element) != element) return;
    SvgTimeline* timeline = svg_animation_timeline(element, true);
    if (!timeline) return;
    timeline->paused = paused;
    if (paused && timeline->driver && !timeline->ticking) {
        DomDocument* doc = timeline->registry->document;
        animation_scheduler_cancel(doc->state->animation_scheduler, timeline->driver);
    } else if (!paused) svg_animation_prepare(element);
}

bool svg_animation_paused(DomElement* element) {
    SvgTimeline* timeline = svg_animation_timeline(element, false);
    return timeline && timeline->paused;
}

static bool svg_animation_rewind_control(const void* data, void* context) {
    SvgTimeline* timeline = (SvgTimeline*)context;
    SvgAnimationControl* control = (SvgAnimationControl*)data;
    DomNode* node = dom_node_ref_validate(timeline->registry->document, control->element);
    if (node && node->is_element() && svg_animation_root(node->as_element()) == timeline->root.address) {
        svg_animation_clear_frozen(timeline->registry, control);
        control->begin_notified = control->end_notified = false;
        control->notified_repeat = 0;
    }
    return true;
}

void svg_animation_set_time(DomElement* element, double seconds) {
    if (!isfinite(seconds) || !element || svg_animation_root(element) != element) return;
    SvgTimeline* timeline = svg_animation_timeline(element, true);
    if (!timeline) return;
    if (seconds < timeline->time) hashmap_scan(timeline->registry->controls, svg_animation_rewind_control, timeline);
    svg_animation_note_time(timeline, fmax(0.0, seconds));
    timeline->origin = timeline->time;
    if (timeline->driver) timeline->driver->start_time = timeline->registry->document->state->animation_scheduler->current_time;
}

double svg_animation_current_time(DomElement* element) {
    SvgTimeline* timeline = svg_animation_timeline(element, false);
    return timeline ? timeline->time : 0.0;
}

void svg_animation_set_document_time(DomDocument* document, double seconds) {
    SvgAnimationRegistry* registry = svg_animation_registry(document, false);
    // private picture/use source documents have no script-controlled fragment clocks.
    for (SvgTimeline* timeline = registry ? registry->timelines : nullptr; timeline; timeline = timeline->next) {
        DomNode* root = dom_node_ref_validate(document, timeline->root);
        if (root && root->is_element()) svg_animation_set_time(root->as_element(), seconds);
    }
}

uint64_t svg_animation_generation(DomDocument* document) {
    SvgAnimationRegistry* registry = svg_animation_registry(document, false);
    return registry ? registry->generation : 0;
}

static SvgAnimatedTarget* svg_animation_target(SvgAnimationRegistry* registry, DomElement* target) {
    SvgAnimatedTarget key = {}; key.source = dom_element_to_element(target);
    SvgAnimatedTarget* entry = SvgAnimatedTargetMap::get(registry->targets, key);
    if (!entry) {
        key.element = dom_node_ref(target);
        SvgAnimatedTargetMap::set(registry->targets, key);
        entry = SvgAnimatedTargetMap::get(registry->targets, key);
    }
    return entry;
}

static void svg_animation_store(SvgAnimationRegistry* registry, DomElement* target,
    const char* name, const char* text, bool css) {
    SvgAnimatedTarget* entry = svg_animation_target(registry, target);
    if (!entry) return;
    SvgAnimatedValue* value = entry->values;
    while (value && (value->css != css || strcmp(value->name, name) != 0)) value = value->next;
    if (!value) {
        value = (SvgAnimatedValue*)svg_animation_alloc(registry, sizeof(*value));
        if (!value) return;
        value->name = svg_animation_copy(registry, name);
        if (!value->name) return;
        value->css = css;
        value->next = entry->values; entry->values = value;
    }
    value->text = text; value->css = css; value->resolved = false;
    Element* source = dom_element_render_source(target);
    if (source && source != entry->source) {
        SvgAnimatedTarget alias = *entry; alias.source = source;
        SvgAnimatedTargetMap::set(registry->targets, alias);
    }
}

static const char* svg_animation_base(SvgAnimationRegistry* registry, DomElement* target,
    const char* name, bool css) {
    if (!css) {
        const char* authored = strcmp(name, "xlink:href") == 0
            ? dom_element_attribute_ns(target, "http://www.w3.org/1999/xlink", "href")
            : target->get_attribute(name);
        if (authored) return authored;
        const char* tag = target->local_name();
        if (svg_animation_name_in("href xlink:href", name)) {
            const char* legacy = strcmp(name, "href") == 0
                ? dom_element_attribute_ns(target, "http://www.w3.org/1999/xlink", "href") : nullptr;
            return legacy ? legacy : "";
        }
        if (strcmp(tag, "feColorMatrix") == 0 && strcmp(name, "values") == 0) {
            const char* type = svg_animation_attribute(target, "type");
            if (svg_animation_keyword(type, "saturate")) return "1";
            if (svg_animation_keyword(type, "hueRotate")) return "0";
            if (!type || svg_animation_keyword(type, "matrix"))
                return "1 0 0 0 0 0 1 0 0 0 0 0 1 0 0 0 0 0 1 0";
        }
        if (strcmp(tag, "rect") == 0 && svg_animation_name_in("rx ry", name)) {
            const char* paired = svg_animation_attribute(target, strcmp(name, "rx") == 0 ? "ry" : "rx");
            if (paired) return paired;
        }
        if (svg_animation_name_in("x y width height", name)) {
            bool size = svg_animation_name_in("width height", name);
            if (svg_animation_filter_primitive(tag)) return size ? "100%" : "0%";
            if (svg_animation_name_in("filter mask", tag)) return size ? "120%" : "-10%";
            if (size && strcmp(tag, "svg") == 0) return "100%";
            if (size && svg_animation_name_in("rect image use foreignObject pattern", tag)) return "0";
        }
        if (strcmp(tag, "radialGradient") == 0 && (strcmp(name, "fx") == 0 || strcmp(name, "fy") == 0)) {
            // focal coordinates default to the current center, including lower animations.
            const char* center = svg_animation_attribute(target, strcmp(name, "fx") == 0 ? "cx" : "cy");
            return center ? center : "50%";
        }
        struct InitialValue { const char* tags; const char* name; const char* value; };
        static const InitialValue initial_values[] = {
            {"stop", "offset", "0"},
            // XML enums need their initial value as the underlying discrete-to sample (SVG 1.1 §19.2.9).
            {"linearGradient radialGradient", "gradientUnits", "objectBoundingBox"},
            {"linearGradient radialGradient", "spreadMethod", "pad"},
            {"pattern", "patternUnits", "objectBoundingBox"},
            {"pattern", "patternContentUnits", "userSpaceOnUse"},
            {"marker", "markerUnits", "strokeWidth"}, {"clipPath", "clipPathUnits", "userSpaceOnUse"},
            {"mask", "maskUnits", "objectBoundingBox"}, {"mask", "maskContentUnits", "userSpaceOnUse"},
            {"filter", "filterUnits", "objectBoundingBox"}, {"filter", "primitiveUnits", "userSpaceOnUse"},
            {"svg symbol image marker pattern view feImage", "preserveAspectRatio", "xMidYMid meet"},
            {"text tspan textPath", "lengthAdjust", "spacing"}, {"textPath", "method", "align"},
            {"textPath", "spacing", "exact"}, {"feComposite", "operator", "over"},
            {"feMorphology", "operator", "erode"}, {"feBlend", "mode", "normal"},
            {"feColorMatrix", "type", "matrix"}, {"feTurbulence", "type", "turbulence"},
            {"feTurbulence", "stitchTiles", "noStitch"},
            {"feFuncR feFuncG feFuncB feFuncA", "type", "identity"},
            {"feConvolveMatrix", "edgeMode", "duplicate"}, {"feConvolveMatrix", "preserveAlpha", "false"},
            {"marker", "orient", "0"}, {"marker", "markerWidth", "3"}, {"marker", "markerHeight", "3"},
            {"marker", "refX", "0"}, {"marker", "refY", "0"}, {"radialGradient", "fr", "0"},
            {"feTurbulence", "numOctaves", "1"}, {"feTurbulence", "baseFrequency", "0 0"},
            {"feTurbulence", "seed", "0"}, {"feGaussianBlur", "stdDeviation", "0 0"},
            {"feDropShadow", "stdDeviation", "2 2"}, {"feDropShadow", "dx", "2"}, {"feDropShadow", "dy", "2"},
            {"feMorphology", "radius", "0 0"}, {"feDisplacementMap", "scale", "0"},
            {"feFuncR feFuncG feFuncB feFuncA", "slope", "1"},
            {"feFuncR feFuncG feFuncB feFuncA", "amplitude", "1"},
            {"feFuncR feFuncG feFuncB feFuncA", "exponent", "1"},
            {"feFuncR feFuncG feFuncB feFuncA", "intercept", "0"},
            {"feFuncR feFuncG feFuncB feFuncA", "offset", "0"},
            {"feDiffuseLighting feSpecularLighting", "surfaceScale", "1"},
            {"feDiffuseLighting", "diffuseConstant", "1"}, {"feSpecularLighting", "specularConstant", "1"},
            {"feSpecularLighting feSpotLight", "specularExponent", "1"},
            {"feDistantLight", "azimuth", "0"}, {"feDistantLight", "elevation", "0"},
            {"fePointLight feSpotLight", "z", "0"}, {"feSpotLight", "pointsAtX", "0"},
            {"feSpotLight", "pointsAtY", "0"}, {"feSpotLight", "pointsAtZ", "0"},
            {"feComposite", "k1", "0"}, {"feComposite", "k2", "0"},
            {"feComposite", "k3", "0"}, {"feComposite", "k4", "0"},
            {"feConvolveMatrix", "order", "3 3"}, {"feConvolveMatrix", "bias", "0"},
        };
        for (const InitialValue& initial : initial_values)
            if (strcmp(name, initial.name) == 0 && svg_animation_name_in(initial.tags, tag)) return initial.value;
        // absent geometry has its SVG initial value, not an unresolved to-animation base.
        static const char* zero_geometry[] = {"x", "y", "cx", "cy", "r", "rx", "ry", "x1", "y1", "x2", "y2", "dx", "dy"};
        for (const char* candidate : zero_geometry) if (strcmp(name, candidate) == 0) {
            if (strcmp(tag, "linearGradient") == 0 && strcmp(name, "x2") == 0) return "100%";
            if (strcmp(tag, "radialGradient") == 0 &&
                (strcmp(name, "cx") == 0 || strcmp(name, "cy") == 0 || strcmp(name, "r") == 0)) return "50%";
            return "0";
        }
        return nullptr;
    }
    char* owned = nullptr;
    bool inherits = false;
    const char* initial = svg_property_initial(name, &inherits);
    const char* base = svg_get_dom_presentation_property(target, name, inherits,
        nullptr, 0, nullptr, &owned);
    const char* result = base || initial ? svg_animation_copy(registry, base ? base : initial) : nullptr;
    lam::Temp<char> dropped(owned);
    return result;
}

struct SvgAnimationVector {
    double* components;
    CssUnit* units;
    unsigned count;
    bool color;
    bool linear_color;
    bool percentage;
    bool list;
    bool integer;
    const char* path_commands;
};

static bool svg_animation_vector_storage(SvgAnimationRegistry* registry,
    SvgAnimationVector* vector, unsigned count) {
    if (!count || count > 2048) return false;
    vector->components = (double*)svg_animation_alloc(registry, sizeof(double) * count);
    if (!vector->components) return false;
    vector->count = count;
    return true;
}

static bool svg_animation_path_vector(SvgAnimationRegistry* registry,
    const char* value, SvgAnimationVector* vector) {
    lam::ArrayList<double> parameters(MEM_CAT_RENDER, 0);
    StrBuf* commands = strbuf_new();
    if (!commands) return false;
    const char* cursor = value;
    char previous = 0;
    bool valid = true;
    while (*(cursor = str_skip_ascii_space(cursor))) {
        char command = *cursor;
        bool explicit_command = str_is_alpha(command);
        if (explicit_command) cursor++;
        else command = previous;
        char upper = command >= 'a' && command <= 'z' ? command - ('a' - 'A') : command;
        float args[7] = {};
        int count = upper == 'Z' ? 0 : svg_path_parameter_count(upper);
        if (!command || (!previous && upper != 'M') || (!explicit_command && upper == 'Z') ||
            count < 0 || commands->length >= 512 || parameters.size() + count > 2048 ||
            (count && !svg_read_path_parameters(&cursor, upper, !explicit_command, args))) {
            valid = false; break;
        }
        strbuf_append_char(commands, command);
        for (int i = 0; i < count; i++) if (!parameters.append(args[i])) valid = false;
        if (!valid) break;
        previous = upper == 'M' ? (command == 'M' ? 'L' : 'l') : command;
    }
    if (valid) valid = svg_animation_vector_storage(registry, vector, parameters.size());
    if (valid) {
        memcpy(vector->components, parameters.data(), parameters.size() * sizeof(double));
        vector->path_commands = svg_animation_copy(registry, commands->str);
        vector->list = true;
        valid = vector->path_commands != nullptr;
    }
    strbuf_free(commands);
    return valid;
}

enum SvgAnimationValueClass {
    SVG_ANIMATION_UNSUPPORTED, SVG_ANIMATION_DISCRETE, SVG_ANIMATION_LENGTH,
    SVG_ANIMATION_NUMBER, SVG_ANIMATION_INTEGER, SVG_ANIMATION_ANGLE,
    SVG_ANIMATION_COLOR, SVG_ANIMATION_PAINT, SVG_ANIMATION_NUMBER_LIST,
    SVG_ANIMATION_LENGTH_LIST, SVG_ANIMATION_PATH,
};

static bool svg_animation_filter_primitive(const char* tag) {
    return svg_animation_name_in("feBlend feColorMatrix feComponentTransfer feComposite feConvolveMatrix "
        "feDiffuseLighting feDisplacementMap feDropShadow feFlood feGaussianBlur feImage feMerge "
        "feMorphology feOffset feSpecularLighting feTile feTurbulence", tag);
}

static bool svg_animation_attribute_applies(DomElement* target, const char* name) {
    const char* tag = target->local_name();
    if (strcmp(name, "result") == 0) return false; // SVG filter result identifiers are not animatable.
    if (svg_animation_name_in("x y width height", name) && svg_animation_filter_primitive(tag)) return true;
    struct AttributeTargets { const char* names; const char* tags; };
    static const AttributeTargets targets[] = {
        {"x y", "svg use image rect foreignObject text tspan pattern mask filter fePointLight feSpotLight"},
        {"width height", "svg use image rect foreignObject pattern mask filter"},
        {"cx cy", "circle ellipse radialGradient"}, {"r", "circle radialGradient"}, {"rx ry", "rect ellipse"},
        {"fx fy fr", "radialGradient"}, {"x1 y1 x2 y2", "line linearGradient"},
        {"dx dy", "text tspan feOffset feDropShadow"}, {"z", "fePointLight feSpotLight"},
        {"pointsAtX pointsAtY pointsAtZ limitingConeAngle", "feSpotLight"}, {"azimuth elevation", "feDistantLight"},
        {"orient refX refY markerWidth markerHeight markerUnits", "marker"},
        {"points", "polygon polyline"}, {"d", "path"}, {"pathLength", "path rect circle ellipse line polygon polyline"},
        {"viewBox", "svg symbol marker pattern view"}, {"rotate", "text tspan"},
        {"startOffset method spacing", "textPath"}, {"textLength lengthAdjust", "text tspan textPath"},
        {"gradientUnits gradientTransform spreadMethod", "linearGradient radialGradient"},
        {"patternUnits patternContentUnits patternTransform", "pattern"}, {"clipPathUnits", "clipPath"},
        {"maskUnits maskContentUnits", "mask"}, {"filterUnits primitiveUnits", "filter"},
        {"offset", "stop feFuncR feFuncG feFuncB feFuncA"},
        {"stdDeviation", "feGaussianBlur feDropShadow"}, {"radius", "feMorphology"},
        {"baseFrequency numOctaves seed stitchTiles", "feTurbulence"}, {"scale", "feDisplacementMap"},
        {"surfaceScale", "feDiffuseLighting feSpecularLighting"}, {"diffuseConstant", "feDiffuseLighting"},
        {"specularConstant", "feSpecularLighting"}, {"specularExponent", "feSpecularLighting feSpotLight"},
        {"kernelUnitLength", "feConvolveMatrix feDiffuseLighting feSpecularLighting"},
        {"order kernelMatrix divisor bias targetX targetY edgeMode preserveAlpha", "feConvolveMatrix"},
        {"amplitude exponent intercept slope tableValues", "feFuncR feFuncG feFuncB feFuncA"},
        {"k1 k2 k3 k4", "feComposite"}, {"operator", "feComposite feMorphology"},
        {"mode", "feBlend"}, {"values", "feColorMatrix"},
        {"type", "feColorMatrix feTurbulence feFuncR feFuncG feFuncB feFuncA"},
        {"in", "feBlend feColorMatrix feComponentTransfer feComposite feConvolveMatrix feDiffuseLighting "
            "feDisplacementMap feDropShadow feGaussianBlur feMergeNode feMorphology feOffset feSpecularLighting feTile"},
        {"in2", "feBlend feComposite feDisplacementMap"},
        {"href xlink:href", "a use image textPath linearGradient radialGradient pattern filter feImage"},
        {"preserveAspectRatio", "svg symbol image marker pattern view feImage"},
    };
    for (const AttributeTargets& entry : targets)
        if (svg_animation_name_in(entry.names, name)) return svg_animation_name_in(entry.tags, tag);
    return true;
}

static SvgAnimationValueClass svg_animation_value_class(DomElement* target, const char* name) {
    if (!svg_animation_attribute_applies(target, name)) return SVG_ANIMATION_UNSUPPORTED;
    struct ClassNames { SvgAnimationValueClass kind; const char* names; };
    static const ClassNames classes[] = {
        {SVG_ANIMATION_PAINT, "fill stroke"},
        {SVG_ANIMATION_COLOR, "color stop-color flood-color lighting-color"},
        {SVG_ANIMATION_ANGLE, "orient glyph-orientation-horizontal glyph-orientation-vertical"},
        {SVG_ANIMATION_INTEGER, "numOctaves targetX targetY"},
        {SVG_ANIMATION_NUMBER, "opacity fill-opacity stroke-opacity stop-opacity flood-opacity offset pathLength stroke-miterlimit scale seed surfaceScale diffuseConstant specularConstant specularExponent limitingConeAngle azimuth elevation bias divisor k1 k2 k3 k4 amplitude exponent intercept slope font-weight"},
        {SVG_ANIMATION_LENGTH, "x y cx cy r rx ry fx fy fr x1 y1 x2 y2 dx dy width height refX refY markerWidth markerHeight startOffset textLength font-size letter-spacing word-spacing stroke-width stroke-dashoffset z pointsAtX pointsAtY pointsAtZ"},
        {SVG_ANIMATION_NUMBER_LIST, "points viewBox rotate stdDeviation baseFrequency kernelUnitLength order radius values kernelMatrix tableValues"},
        {SVG_ANIMATION_LENGTH_LIST, "stroke-dasharray"},
        {SVG_ANIMATION_PATH, "d"},
        {SVG_ANIMATION_DISCRETE, "href xlink:href preserveAspectRatio gradientUnits gradientTransform spreadMethod patternUnits patternContentUnits patternTransform markerUnits clipPathUnits maskUnits maskContentUnits filterUnits primitiveUnits in in2 operator type mode edgeMode stitchTiles preserveAlpha result lengthAdjust method spacing"},
    };
    const char* tag = target->local_name();
    bool text = strcmp(tag, "text") == 0 || strcmp(tag, "tspan") == 0 || strcmp(tag, "textPath") == 0;
    if (text && (strcmp(name, "x") == 0 || strcmp(name, "y") == 0 || strcmp(name, "dx") == 0 || strcmp(name, "dy") == 0))
        return SVG_ANIMATION_LENGTH_LIST;
    for (const ClassNames& entry : classes)
        if (svg_animation_name_in(entry.names, name)) return entry.kind;
    bool inherits = false;
    return svg_property_initial(name, &inherits) ? SVG_ANIMATION_DISCRETE : SVG_ANIMATION_UNSUPPORTED;
}

static bool svg_animation_vector(SvgAnimationRegistry* registry, DomElement* target, const char* name,
    const char* value, const char* transform_type, SvgAnimationVector* vector, bool linear_color = false) {
    if (!value || strlen(value) > 65536) return false;
    SvgAnimationValueClass kind = svg_animation_value_class(target, name);
    CssColor color = {};
    bool is_color = !transform_type && (kind == SVG_ANIMATION_COLOR || kind == SVG_ANIMATION_PAINT) && css_parse_color(value, &color);
    if (is_color && color.type == CSS_COLOR_CURRENT) {
        char buffer[256];
        const char* current = svg_get_dom_presentation_property(target, "color", true,
            buffer, sizeof(buffer));
        is_color = css_parse_color(current ? current : "black", &color) && color.type != CSS_COLOR_CURRENT;
    }
    if (is_color) {
        if (!svg_animation_vector_storage(registry, vector, 4)) return false;
        vector->color = true;
        vector->components[0] = color.r; vector->components[1] = color.g;
        vector->components[2] = color.b; vector->components[3] = color.a;
        vector->linear_color = linear_color;
        if (linear_color) for (unsigned i = 0; i < 3; i++)
            vector->components[i] = render_color_srgb_to_linear(vector->components[i] / 255) * 255;
        return true;
    }
    if (transform_type) {
        float components[3] = {};
        const char* end = nullptr;
        size_t count = str_parse_float_list(value, ", \t\n\r", components, 3, &end);
        if (!count || *str_skip_chars(end, ", \t\n\r")) return false;
        bool scale = strcmp(transform_type, "scale") == 0;
        bool translate = strcmp(transform_type, "translate") == 0;
        bool rotate = strcmp(transform_type, "rotate") == 0;
        bool skew = strcmp(transform_type, "skewX") == 0 || strcmp(transform_type, "skewY") == 0;
        if (!(scale || translate || rotate || skew) ||
            ((scale || translate) && count > 2) || (rotate && count == 2) || (skew && count != 1)) return false;
        if (scale && count == 1) components[1] = components[0];
        if (!svg_animation_vector_storage(registry, vector, rotate ? 3 : scale || translate ? 2 : 1)) return false;
        for (unsigned i = 0; i < vector->count; i++) {
            if (!isfinite(components[i])) return false;
            vector->components[i] = components[i];
        }
        return true;
    }
    if (kind == SVG_ANIMATION_PATH) return svg_animation_path_vector(registry, value, vector);
    if (kind == SVG_ANIMATION_NUMBER_LIST || kind == SVG_ANIMATION_LENGTH_LIST) {
        float components[2048]; const char* end = nullptr;
        CssUnit units[2048] = {};
        size_t count = 0;
        if (kind == SVG_ANIMATION_NUMBER_LIST) count = str_parse_float_list(value, ", \t\n\r", components, 2048, &end);
        else {
            SvgLengthContext lengths = dom_svg_length_context(target);
            end = str_skip_ascii_space(value);
            while (*end && count < 2048) {
                const char* start = end;
                char* number_end = nullptr;
                double number = strtod(start, &number_end);
                if (number_end == start) return false;
                end = number_end;
                while (str_is_alpha(*end) || *end == '%') end++;
                CssUnit unit = css_unit_from_string(number_end, end - number_end);
                if (end != number_end && unit == CSS_UNIT_NONE) return false;
                if (!isfinite(svg_resolve_length_unit(number, unit, &lengths, dom_svg_length_axis(name), NAN))) return false;
                components[count] = number; units[count++] = unit;
                const char* separator = end;
                end = str_skip_chars(end, ", \t\n\r");
                if (*end && separator == end && *end != '+' && *end != '-') return false;
            }
        }
        bool pair = strcmp(name, "stdDeviation") == 0 || strcmp(name, "baseFrequency") == 0 ||
            strcmp(name, "kernelUnitLength") == 0 || strcmp(name, "order") == 0 || strcmp(name, "radius") == 0;
        if (pair && count == 1) { components[1] = components[0]; count = 2; }
        if (!count || *str_skip_chars(end, ", \t\n\r") ||
            (pair && count != 2) ||
            (strcmp(name, "points") == 0 && count % 2) ||
            (strcmp(name, "viewBox") == 0 && (count != 4 || components[2] < 0 || components[3] < 0)) ||
            !svg_animation_vector_storage(registry, vector, count)) return false;
        for (unsigned i = 0; i < count; i++) {
            if (!isfinite(components[i]) || (pair && components[i] < 0) ||
                (strcmp(name, "order") == 0 && (components[i] <= 0 || components[i] != floor(components[i])))) return false;
            vector->components[i] = components[i];
        }
        vector->list = true;
        if (kind == SVG_ANIMATION_LENGTH_LIST) {
            vector->units = (CssUnit*)svg_animation_alloc(registry, count * sizeof(CssUnit));
            if (!vector->units) return false;
            memcpy(vector->units, units, count * sizeof(CssUnit));
        }
        vector->integer = strcmp(name, "order") == 0;
        return true;
    }
    if (kind != SVG_ANIMATION_LENGTH && kind != SVG_ANIMATION_NUMBER &&
        kind != SVG_ANIMATION_INTEGER && kind != SVG_ANIMATION_ANGLE) return false;
    char* end = nullptr;
    double number = strtod(value, &end);
    if (strcmp(name, "font-weight") == 0 && svg_animation_name_in("normal bold bolder lighter", value)) {
        DomElement* parent = target->parent && target->parent->is_element() ? target->parent->as_element() : nullptr;
        char buffer[64];
        const char* inherited = parent ? svg_get_dom_presentation_property(parent, name, true, buffer, sizeof(buffer)) : nullptr;
        number = svg_font_weight_value(value, inherited ? svg_font_weight_value(inherited, 400) : 400);
        end = (char*)value + strlen(value);
    }
    if (end != value && *str_skip_ascii_space(end) == '%' &&
        !*str_skip_ascii_space(str_skip_ascii_space(end) + 1) && isfinite(number)) {
        if (kind != SVG_ANIMATION_LENGTH && strcmp(name, "offset") != 0 && !strstr(name, "opacity")) return false;
        // retain percentages until the resource user's object box or viewport is known.
        if (!svg_animation_vector_storage(registry, vector, 1)) return false;
        vector->percentage = true; vector->components[0] = number;
        return true;
    }
    SvgLengthContext lengths = dom_svg_length_context(target);
    double scalar = kind == SVG_ANIMATION_ANGLE ? svg_resolve_angle(value, NAN) :
        kind == SVG_ANIMATION_LENGTH ? svg_resolve_length(value, &lengths, dom_svg_length_axis(name), NAN) :
        end != value && !*str_skip_ascii_space(end) ? number : NAN;
    if (!isfinite(scalar)) return false;
    if (strcmp(name, "font-weight") == 0 && (scalar < 1 || scalar > 1000)) return false;
    if (kind == SVG_ANIMATION_INTEGER && scalar != floor(scalar)) return false;
    if (!svg_animation_vector_storage(registry, vector, 1)) return false;
    vector->integer = kind == SVG_ANIMATION_INTEGER;
    vector->components[0] = scalar;
    if (kind == SVG_ANIMATION_LENGTH && end != value) {
        const char* unit_text = str_skip_ascii_space(end);
        size_t unit_length = strlen(unit_text); str_trim(&unit_text, &unit_length);
        CssUnit unit = css_unit_from_string(unit_text, unit_length);
        if (unit != CSS_UNIT_NONE) {
            vector->units = (CssUnit*)svg_animation_alloc(registry, sizeof(CssUnit));
            if (!vector->units) return false;
            vector->units[0] = unit; vector->components[0] = number;
        }
    }
    return true;
}

static bool svg_animation_vectors_compatible(const SvgAnimationVector* a, const SvgAnimationVector* b) {
    return a->count == b->count && a->color == b->color && a->list == b->list &&
        ((!a->path_commands && !b->path_commands) || (a->path_commands && b->path_commands &&
            strcmp(a->path_commands, b->path_commands) == 0));
}

static const char* svg_animation_vector_text(SvgAnimationRegistry* registry,
    const SvgAnimationVector* vector, const char* transform_type) {
    StrBuf* text = strbuf_new();
    if (!text) return nullptr;
    if (vector->color) {
        double channels[3];
        for (unsigned i = 0; i < 3; i++) channels[i] = vector->linear_color
            ? render_color_linear_to_srgb(vector->components[i] / 255) * 255 : vector->components[i];
        strbuf_append_format(text, "rgba(%g,%g,%g,%g)", channels[0], channels[1], channels[2], vector->components[3] / 255.0);
    }
    else if (vector->path_commands) {
        unsigned offset = 0;
        for (const char* command = vector->path_commands; *command; command++) {
            char upper = *command >= 'a' && *command <= 'z' ? *command - ('a' - 'A') : *command;
            int count = upper == 'Z' ? 0 : svg_path_parameter_count(upper);
            strbuf_append_char(text, *command);
            for (int i = 0; i < count; i++) {
                // SVG path flags interpolate numerically; every nonzero sample is true.
                double value = vector->components[offset++];
                if (upper == 'A' && (i == 3 || i == 4)) value = value != 0;
                strbuf_append_format(text, " %g", value);
            }
        }
    } else {
        if (transform_type) { strbuf_append_str(text, transform_type); strbuf_append_char(text, '('); }
        for (unsigned i = 0; i < vector->count; i++) {
            if (i) strbuf_append_char(text, ' ');
            strbuf_append_format(text, "%g", vector->integer ? round(vector->components[i]) : vector->components[i]);
            if (vector->percentage) strbuf_append_char(text, '%');
            else if (vector->units) strbuf_append_str(text, css_unit_to_string(vector->units[i]));
        }
        if (transform_type) strbuf_append_char(text, ')');
    }
    const char* result = svg_animation_copy(registry, text->str);
    strbuf_free(text);
    return result;
}

static bool svg_animation_align_lengths(DomElement* target, const char* name,
    SvgAnimationVector* a, SvgAnimationVector* b, bool absolute = false) {
    if (a->percentage == b->percentage && !a->units && !b->units && !absolute) return true;
    SvgLengthContext lengths = dom_svg_length_context(target);
    if (a->percentage != b->percentage || (absolute && a->percentage)) {
        // opacity/offset percentages are dimensionless; length percentages use the SVG viewport.
        float basis = svg_animation_value_class(target, name) == SVG_ANIMATION_LENGTH
            ? svg_resolve_length("100%", &lengths, dom_svg_length_axis(name), NAN) / 100 : .01f;
        if (!isfinite(basis)) return false;
        if (a->percentage) a->components[0] *= basis;
        if (b->percentage) b->components[0] *= basis;
        a->percentage = b->percentage = false;
    }
    for (unsigned i = 0; i < a->count; i++) {
        CssUnit left = a->units ? a->units[i] : CSS_UNIT_NONE;
        CssUnit right = b->units ? b->units[i] : CSS_UNIT_NONE;
        if (left == right && (!absolute || left == CSS_UNIT_NONE)) continue;
        a->components[i] = svg_resolve_length_unit(a->components[i], left, &lengths, dom_svg_length_axis(name), NAN);
        b->components[i] = svg_resolve_length_unit(b->components[i], right, &lengths, dom_svg_length_axis(name), NAN);
        if (!isfinite(a->components[i]) || !isfinite(b->components[i])) return false;
        if (a->units) a->units[i] = CSS_UNIT_NONE;
        if (b->units) b->units[i] = CSS_UNIT_NONE;
    }
    return true;
}

static const char* svg_animation_combine(SvgAnimationRegistry* registry, DomElement* target,
    const char* name, const char* left, const char* right, double left_weight, double right_weight,
    const char* transform_type, bool linear_color = false) {
    SvgAnimationVector a = {}, b = {};
    if (!svg_animation_vector(registry, target, name, left, transform_type, &a, linear_color) ||
        !svg_animation_vector(registry, target, name, right, transform_type, &b, linear_color) ||
        !svg_animation_vectors_compatible(&a, &b)) return nullptr;
    // retain equal units so later font/viewport samples determine the target's used value.
    if (!svg_animation_align_lengths(target, name, &a, &b)) return nullptr;
    if (!animation_value_combine(a.components, b.components, a.count, left_weight, right_weight)) return nullptr;
    // component values stay unclamped until the target's normal used-value resolution.
    return svg_animation_vector_text(registry, &a, nullptr);
}

static bool svg_animation_list_begin(StrSplitIter* split, const char* text, bool trailing_separator = false) {
    if (!text || !*text || strlen(text) > 65536) return false;
    const char* end = text + strlen(text);
    while (end > text && str_char_is_ascii_space(end[-1])) end--;
    if (trailing_separator && end > text && end[-1] == ';') {
        end--;
        while (end > text && str_char_is_ascii_space(end[-1])) end--;
    }
    if (end == text || end[-1] == ';') return false;
    str_split_byte_init(split, text, end - text, ';');
    return true;
}

static bool svg_animation_list_next(StrSplitIter* split, const char** start, size_t* length) {
    if (!str_split_next(split, start, length)) return false;
    const char* end = *start + *length;
    *start = strn_skip_ascii_space(*start, end);
    while (end > *start && str_char_is_ascii_space(end[-1])) end--;
    *length = end - *start;
    return true;
}

static unsigned svg_animation_list(SvgAnimationRegistry* registry, const char* text,
    const char** values, unsigned capacity) {
    StrSplitIter split;
    if (!svg_animation_list_begin(&split, text, true)) return 0;
    unsigned count = 0;
    const char* start; size_t length;
    while (svg_animation_list_next(&split, &start, &length)) {
        if (!length || count == capacity) return 0;
        char* value = (char*)svg_animation_alloc(registry, length + 1);
        if (!value) return 0;
        memcpy(value, start, length); values[count++] = value;
    }
    return count;
}

static double svg_animation_number(const char* value, double fallback) {
    if (!value) return fallback;
    char* end = nullptr;
    double number = strtod(value, &end);
    return end != value && !*str_skip_ascii_space(end) && isfinite(number) ? number : fallback;
}

static const char* svg_animation_underlying(SvgAnimationRegistry* registry,
    DomElement* target, const char* name, bool css) {
    SvgAnimatedTarget* entry = svg_animation_target(registry, target);
    for (SvgAnimatedValue* value = entry ? entry->values : nullptr; value; value = value->next)
        if (value->css == css && strcmp(value->name, name) == 0) return value->text;
    return svg_animation_base(registry, target, name, css);
}

static bool svg_animation_value_valid(DomElement* target, const char* name,
    const char* value, bool numeric) {
    if (numeric) return true;
    if (!value) return false;
    // an absent IRI has an empty underlying value; it still transitions to a discrete resource link.
    if (!*value) return svg_animation_name_in("href xlink:href", name);
    SvgAnimationValueClass kind = svg_animation_value_class(target, name);
    if (kind == SVG_ANIMATION_UNSUPPORTED) return false;
    if (svg_animation_keyword(value, "inherit") || svg_animation_keyword(value, "initial") ||
        svg_animation_keyword(value, "unset")) return true;
    if (kind == SVG_ANIMATION_DISCRETE) {
        struct EnumValues { const char* names; const char* values; };
        static const EnumValues enums[] = {
            {"gradientUnits patternUnits patternContentUnits clipPathUnits maskUnits maskContentUnits filterUnits primitiveUnits", "userSpaceOnUse objectBoundingBox"},
            {"spreadMethod", "pad reflect repeat"}, {"markerUnits", "strokeWidth userSpaceOnUse"},
            {"lengthAdjust", "spacing spacingAndGlyphs"}, {"method", "align stretch"}, {"spacing", "auto exact"},
            {"edgeMode", "duplicate wrap none"}, {"stitchTiles", "stitch noStitch"}, {"preserveAlpha", "true false"},
            {"fill-rule clip-rule", "nonzero evenodd"}, {"stroke-linecap", "butt round square"},
            {"stroke-linejoin", "miter round bevel miter-clip arcs"}, {"visibility", "visible hidden collapse"},
            {"color-interpolation color-interpolation-filters", "auto sRGB linearRGB"},
            {"mask-type", "luminance alpha"}, {"text-anchor", "start middle end"},
        };
        for (const EnumValues& entry : enums)
            if (svg_animation_name_in(entry.names, name)) return svg_animation_name_in(entry.values, value);
        if (strcmp(name, "operator") == 0) return svg_animation_name_in(
            strcmp(target->local_name(), "feMorphology") == 0 ? "erode dilate" : "over in out atop xor arithmetic lighter", value);
        if (strcmp(name, "mode") == 0) return svg_animation_name_in(
            "normal multiply screen darken lighten overlay color-dodge color-burn hard-light soft-light difference exclusion hue saturation color luminosity", value);
        if (strcmp(name, "type") == 0) return svg_animation_name_in(
            strcmp(target->local_name(), "feColorMatrix") == 0 ? "matrix saturate hueRotate luminanceToAlpha" :
            strcmp(target->local_name(), "feTurbulence") == 0 ? "fractalNoise turbulence" : "identity table discrete linear gamma", value);
        return true;
    }
    if (kind == SVG_ANIMATION_PAINT) return svg_paint_value_is_valid(value);
    if (kind == SVG_ANIMATION_ANGLE && (svg_animation_keyword(value, "auto") ||
        svg_animation_keyword(value, "auto-start-reverse"))) return true;
    if (kind == SVG_ANIMATION_LENGTH && (svg_animation_keyword(value, "auto") ||
        svg_animation_keyword(value, "normal"))) return true;
    return kind == SVG_ANIMATION_LENGTH_LIST && svg_animation_keyword(value, "none");
}

static const char* svg_animation_sample_values(SvgTimeline* timeline, DomElement* animation,
    DomElement* target, const char* name, bool css, float progress, double iteration, double duration) {
    SvgAnimationRegistry* registry = timeline->registry;
    const char* transform_type = strcmp(animation->local_name(), "animateTransform") == 0
        ? svg_animation_token(registry, animation->get_attribute("type")) : nullptr;
    if (strcmp(animation->local_name(), "animateTransform") == 0 && !transform_type) transform_type = "translate";
    char color_space[32];
    const char* interpolation = svg_get_dom_presentation_property(animation, "color-interpolation", true,
        color_space, sizeof(color_space));
    // css computation canonicalizes the keyword before SMIL consumes its color space.
    bool linear_color = interpolation && str_ieq_cstr(interpolation, "linearRGB");
    const char* values[256];
    unsigned count = 0;
    const char* list = animation->get_attribute("values");
    const char* from = svg_animation_token(registry, animation->get_attribute("from"));
    const char* to = svg_animation_token(registry, animation->get_attribute("to"));
    const char* by = svg_animation_token(registry, animation->get_attribute("by"));
    const char* additive = animation->get_attribute("additive");
    bool sum = svg_animation_keyword(additive, "sum");
    bool to_only = !list && !from && to;
    const char* underlying = svg_animation_underlying(registry, target, name, css);
    if (list) count = svg_animation_list(registry, list, values, 256);
    else {
        if (!from && by && !to) {
            SvgAnimationVector zero = {};
            if (!svg_animation_vector(registry, target, name, by, transform_type, &zero, linear_color) || zero.list) return nullptr;
            // SMIL's zero is defined by the value class, including RGB and transform vectors.
            memset(zero.components, 0, zero.count * sizeof(double));
            from = svg_animation_vector_text(registry, &zero, nullptr);
        } else if (!from) from = underlying;
        if (!to && by && from) {
            SvgAnimationVector delta = {};
            if (!svg_animation_vector(registry, target, name, by, transform_type, &delta, linear_color) || delta.list) return nullptr;
            to = svg_animation_combine(registry, target, name, from, by, 1, 1, transform_type, linear_color);
        }
        if (!from || !to) return nullptr;
        values[0] = from; values[1] = to; count = 2;
        if (by && !animation->get_attribute("from") && !animation->get_attribute("to")) sum = true;
    }
    if (!count) return nullptr;
    const char* mode = svg_animation_token(registry, animation->get_attribute("calcMode"));
    if (mode && strcmp(mode, "discrete") != 0 && strcmp(mode, "linear") != 0 &&
        strcmp(mode, "paced") != 0 && strcmp(mode, "spline") != 0) return nullptr;
    bool discrete = mode && strcmp(mode, "discrete") == 0;
    bool paced = mode && strcmp(mode, "paced") == 0;
    bool spline = mode && strcmp(mode, "spline") == 0;
    SvgAnimationVector first = {}, next = {};
    bool numeric = svg_animation_vector(registry, target, name, values[0], transform_type, &first, linear_color);
    if (!svg_animation_value_valid(target, name, values[0], numeric)) return nullptr;
    bool additive_class = numeric && !first.list;
    double distances[256] = {};
    for (unsigned i = 1; i < count; i++) {
        bool valid = svg_animation_vector(registry, target, name, values[i], transform_type, &next, linear_color);
        if (!svg_animation_value_valid(target, name, values[i], valid)) return nullptr;
        if (valid && numeric && !svg_animation_vectors_compatible(&first, &next) && first.path_commands)
            return nullptr; // SVG 1.1 §8.2 requires matching path command structures.
        if (!valid || !numeric || !svg_animation_vectors_compatible(&first, &next)) numeric = false;
        if (numeric) {
            if (paced && !svg_animation_align_lengths(target, name, &first, &next, true)) return nullptr;
            unsigned components = transform_type && first.count == 3 ? 1 : first.color ? 3 : first.count;
            double squared = 0;
            for (unsigned c = 0; c < components; c++) {
                double difference = first.components[c] - next.components[c]; squared += difference * difference;
            }
            distances[i] = distances[i-1] + sqrt(squared);
        }
        first = next;
    }
    if (numeric && first.list && paced) paced = false;
    if (!numeric) { discrete = true; paced = spline = false; }
    // SVG 1.1 §19.2.9 makes discrete to-animations use the same boundary as from-to.
    double times[256] = {};
    const char* key_times = animation->get_attribute("keyTimes");
    // SMIL §3.2.3 ignores keyTimes when the simple duration is indefinite.
    if (key_times && !paced && isfinite(duration)) {
        const char* authored[256];
        if (svg_animation_list(registry, key_times, authored, 256) != count) return nullptr;
        for (unsigned i = 0; i < count; i++) {
            times[i] = svg_animation_number(authored[i], NAN);
            if (!isfinite(times[i]) || times[i] < 0 || times[i] > 1 || (i && times[i] < times[i-1])) return nullptr;
        }
        if (times[0] != 0 || (!discrete && count > 1 && times[count-1] != 1)) return nullptr;
    } else for (unsigned i = 1; i < count; i++) {
        times[i] = paced && distances[count-1] > 0 ? distances[i] / distances[count-1] :
            (double)i / (discrete ? count : count - 1);
    }
    unsigned segment = animation_keyframe_segment(times, count, progress);
    if (progress == 1) segment = count - 1;
    float local = segment + 1 < count && times[segment+1] > times[segment]
        ? (progress - times[segment]) / (times[segment+1] - times[segment]) : 0;
    if (spline && count > 1) {
        const char* authored[256];
        if (svg_animation_list(registry, animation->get_attribute("keySplines"), authored, 256) != count - 1) return nullptr;
        for (unsigned i = 0; i + 1 < count; i++) {
            float controls[4]; const char* end = nullptr;
            if (str_parse_float_list(authored[i], ", \t\n\r", controls, 4, &end) != 4 ||
                *str_skip_chars(end, ", \t\n\r")) return nullptr;
            for (float control : controls) if (!isfinite(control) || control < 0 || control > 1) return nullptr;
            if (i == segment) {
                TimingFunction timing = {};
                timing_cubic_bezier_init(&timing, controls[0], controls[1], controls[2], controls[3]);
                local = timing_function_eval(&timing, local);
            }
        }
    }
    const char* result = !discrete && segment + 1 < count
        ? svg_animation_combine(registry, target, name, values[segment], values[segment+1], 1 - local, local, transform_type, linear_color)
        : svg_animation_copy(registry, values[segment]);
    const char* accumulate = animation->get_attribute("accumulate");
    if (result && additive_class && numeric && !to_only && iteration > 0 && svg_animation_keyword(accumulate, "sum"))
        result = svg_animation_combine(registry, target, name, result, values[count-1], 1, iteration, transform_type, linear_color);
    if (result && additive_class && numeric && sum && !to_only && !transform_type && underlying)
        result = svg_animation_combine(registry, target, name, underlying, result, 1, 1, nullptr, linear_color);
    if (result && transform_type) {
        SvgAnimationVector vector = {};
        if (!svg_animation_vector(registry, target, name, result, transform_type, &vector)) return nullptr;
        result = svg_animation_vector_text(registry, &vector, transform_type);
        if (result && sum && underlying && strcmp(underlying, "none") != 0) {
            StrBuf* text = strbuf_new();
            if (!text) return nullptr;
            strbuf_append_str(text, underlying); strbuf_append_char(text, ' '); strbuf_append_str(text, result);
            result = svg_animation_copy(registry, text->str); strbuf_free(text);
        }
    }
    return result;
}

struct SvgAnimationInterval {
    double begin;
    double end;
    double duration;
    double repeating_duration;
};

struct SvgAnimationReference {
    DomElement* element;
    char symbol[1024];
    double offset;
    bool explicit_id;
};

DomElement* svg_animation_target_element(DomElement* animation) {
    if (!animation || !svg_animation_is_element(animation)) return nullptr;
    const char* href = animation->get_attribute("href");
    if (!href) href = dom_element_attribute_ns(animation, "http://www.w3.org/1999/xlink", "href");
    if (!href) href = animation->get_attribute("xlink:href");
    DomElement* target = href ? (href[0] == '#' ? dom_find_element_by_id(animation->doc->root, href + 1) : nullptr) :
        animation->parent && animation->parent->is_element() ? animation->parent->as_element() : nullptr;
    return target && svg_animation_root(target) == svg_animation_root(animation) ? target : nullptr;
}

static const char* svg_animation_attribute_name(DomElement* animation) {
    const char* name = animation->get_attribute("attributeName");
    if (!name) return nullptr;
    const char* colon = strchr(name, ':');
    if (!colon) return name;
    char prefix[128]; size_t length = colon - name;
    if (!length || length >= sizeof(prefix)) return nullptr;
    memcpy(prefix, name, length); prefix[length] = 0;
    // SVG 1.1 19.2.6 resolves the animation's QName independently of the target's prefix.
    const char* uri = dom_element_lookup_namespace_uri(animation, prefix);
    return uri && strcmp(uri, "http://www.w3.org/1999/xlink") == 0 && strcmp(colon + 1, "href") == 0
        ? "xlink:href" : nullptr;
}

static bool svg_animation_reference(DomElement* animation, const char* value,
    SvgAnimationReference* reference) {
    if (!value || !*value || strlen(value) >= sizeof(reference->symbol) || strcmp(value, "indefinite") == 0) return false;
    const char* finish = value + strlen(value);
    reference->offset = 0;
    for (const char* candidate = finish; candidate > value;) {
        candidate--;
        if (*candidate != '+' && *candidate != '-') continue;
        double offset = svg_animation_clock_value(candidate, NAN);
        if (isfinite(offset)) { reference->offset = offset; finish = candidate; break; }
    }
    while (finish > value && str_char_is_ascii_space(finish[-1])) finish--;
    const char* dot = nullptr;
    for (const char* cursor = value; cursor < finish; cursor++) {
        if (*cursor == '\\' && cursor + 1 < finish) cursor++;
        else if (*cursor == '.') dot = cursor;
    }
    if (dot) {
        reference->explicit_id = true;
        char identifier[1024]; size_t count = 0;
        for (const char* cursor = value; cursor < dot; cursor++) {
            if (*cursor == '\\' && cursor + 1 < dot) cursor++;
            identifier[count++] = *cursor;
        }
        identifier[count] = 0;
        reference->element = dom_find_element_by_id(animation->doc->root, identifier);
        value = dot + 1;
    } else reference->element = svg_animation_target_element(animation);
    size_t count = 0;
    for (const char* cursor = value; cursor < finish; cursor++) {
        if (*cursor == '\\' && cursor + 1 < finish) cursor++;
        reference->symbol[count++] = *cursor;
    }
    reference->symbol[count] = 0;
    return reference->element && count;
}

struct SvgAnimationTimingEntry {
    DomElement* element;
    SvgAnimationInterval intervals[256];
    unsigned count;
    unsigned pass;
    double horizon;
    bool evaluating;
};

struct SvgAnimationTimingQuery {
    lam::ArrayList<SvgAnimationTimingEntry*> entries;
    size_t initial;
    size_t remaining;
    unsigned pass = 0;
    bool changed = false;
    bool failed = false;
    SvgAnimationTimingQuery() : entries(MEM_CAT_RENDER, 8),
        initial(svg_animation_work && svg_animation_work->remaining < 16384 ? svg_animation_work->remaining : 16384),
        remaining(initial) {}
    ~SvgAnimationTimingQuery() {
        if (svg_animation_work) svg_animation_work->remaining -= initial - remaining;
        for (SvgAnimationTimingEntry* entry : entries) lam::Temp<SvgAnimationTimingEntry> owned(entry);
    }
};

static SvgAnimationTimingEntry* svg_animation_timing_entry(SvgTimeline* timeline,
    DomElement* animation, double horizon, SvgAnimationTimingQuery* query, unsigned depth);

static bool svg_animation_insert_time(double* times, unsigned* count, unsigned capacity, double time) {
    if (!isfinite(time)) return true;
    unsigned position = 0;
    while (position < *count && times[position] < time) position++;
    if (position < *count && times[position] == time) return true;
    if (*count == capacity) return false;
    for (unsigned index = *count; index > position; index--) times[index] = times[index-1];
    times[position] = time; (*count)++;
    return true;
}

static unsigned svg_animation_offset_times(SvgTimeline* timeline, DomElement* animation,
    const char* text, double* times, unsigned capacity, double omitted,
    SvgAnimationTimingQuery* query, double horizon, unsigned depth) {
    if (!text) {
        if (isfinite(omitted)) { times[0] = omitted; return 1; }
        return 0;
    }
    StrSplitIter split;
    if (!svg_animation_list_begin(&split, text)) return 0;
    unsigned resolved = 0;
    const char* start; size_t length;
    while (query->remaining && !query->failed && svg_animation_list_next(&split, &start, &length)) {
        query->remaining--;
        if (!length || length >= 1024) continue;
        // timing queries return numbers only; their tokens do not accumulate in the paint sample pool.
        char token[1024]; memcpy(token, start, length); token[length] = 0;
        double time = svg_animation_clock_value(token, NAN);
        if (!isfinite(time)) time = svg_animation_wallclock_value(token, timeline->wallclock_origin);
        if (isfinite(time)) query->failed = !svg_animation_insert_time(times, &resolved, capacity, time);
        else {
            SvgAnimationReference reference = {};
            if (!svg_animation_reference(animation, token, &reference) ||
                !svg_animation_is_element(reference.element) ||
                svg_animation_root(reference.element) != timeline->root.address) continue;
            bool begin = strcmp(reference.symbol, "begin") == 0;
            if (!begin && strcmp(reference.symbol, "end") != 0) continue;
            // negative sync offsets can make a future interval relevant to the current sample.
            SvgAnimationTimingEntry* referenced = svg_animation_timing_entry(timeline, reference.element,
                horizon + fmax(0, -reference.offset), query, depth + 1);
            for (unsigned r = 0; referenced && r < referenced->count && !query->failed; r++) {
                if (!query->remaining) { query->failed = true; break; }
                query->remaining--;
                query->failed = !svg_animation_insert_time(times, &resolved, capacity,
                    (begin ? referenced->intervals[r].begin : referenced->intervals[r].end) + reference.offset);
            }
        }
    }
    return resolved;
}

static unsigned svg_animation_calculate_intervals(SvgTimeline* timeline, DomElement* animation,
    SvgAnimationInterval* intervals, unsigned capacity, SvgAnimationTimingQuery* query,
    double horizon, unsigned depth, const SvgAnimationTimingEntry* previous) {
    if (!query->remaining) { query->failed = true; return 0; }
    query->remaining--;
    const char* duration_text = animation->get_attribute("dur");
    double duration = svg_animation_clock_value(duration_text,
        duration_text && !svg_animation_keyword(duration_text, "media") ? NAN : INFINITY);
    if (isnan(duration) || duration <= 0) return 0;
    const char* repeat_count = animation->get_attribute("repeatCount");
    const char* repeat_duration_text = animation->get_attribute("repeatDur");
    double repeats = svg_animation_keyword(repeat_count, "indefinite") ? INFINITY :
        svg_animation_number(repeat_count, repeat_duration_text ? INFINITY : 1.0);
    double repeat_duration = svg_animation_clock_value(repeat_duration_text, INFINITY);
    if (repeats <= 0 || repeat_duration <= 0) return 0;
    double repeating = fmin(duration * repeats, repeat_duration);
    double minimum = svg_animation_clock_value(animation->get_attribute("min"), 0);
    double maximum = svg_animation_clock_value(animation->get_attribute("max"), INFINITY);
    if (minimum < 0) minimum = 0;
    if (maximum <= 0) maximum = INFINITY;
    if (minimum > maximum) { minimum = 0; maximum = INFINITY; }
    double begins[256], ends[256];
    unsigned begin_count = svg_animation_offset_times(timeline, animation,
        animation->get_attribute("begin"), begins, 256, 0, query, horizon, depth);
    // SMIL §3.6.8: a begun interval keeps its begin; a restart cannot retract its own syncbase.
    for (unsigned i = 0; i < previous->count; i++)
        if (!svg_animation_insert_time(begins, &begin_count, 256, previous->intervals[i].begin)) query->failed = true;
    unsigned end_count = svg_animation_offset_times(timeline, animation,
        animation->get_attribute("end"), ends, 256, INFINITY, query, horizon, depth);
    SvgAnimationControl* controls[2] = {svg_animation_control(timeline->registry, animation, false), nullptr};
    if (timeline->registry->instance_host.address) {
        SvgAnimationRegistry* original = (SvgAnimationRegistry*)animation->doc->services.svg_animation_registry;
        if (original) controls[1] = svg_animation_control(original, animation, false);
    }
    for (unsigned c = 0; c < 2; c++)
        for (SvgAnimationInstanceTime* time = controls[c] ? controls[c]->times : nullptr; time; time = time->next)
            if (!time->end && (!c || time->broadcast) &&
                !svg_animation_insert_time(begins, &begin_count, 256, time->value)) query->failed = true;
    const char* restart = animation->get_attribute("restart");
    bool never = svg_animation_keyword(restart, "never");
    bool inactive = svg_animation_keyword(restart, "whenNotActive");
    unsigned resolved = 0;
    for (unsigned i = 0; i < begin_count && resolved < capacity && query->remaining; i++) {
        query->remaining--;
        double begin = begins[i];
        if (begin > horizon) break;
        if ((resolved && never) || (resolved && inactive && begin < intervals[resolved-1].end)) continue;
        double explicit_end = INFINITY;
        for (unsigned e = 0; e < end_count; e++) if (ends[e] >= begin) { explicit_end = ends[e]; break; }
        for (unsigned c = 0; c < 2; c++)
            for (SvgAnimationInstanceTime* time = controls[c] ? controls[c]->times : nullptr; time; time = time->next)
                if (time->end && (!c || time->broadcast) && time->resolved_at >= begin && time->value >= begin)
                    explicit_end = fmin(explicit_end, time->value);
        double active = fmin(repeating, explicit_end - begin);
        active = fmin(maximum, fmax(minimum, active));
        double end = begin + active;
        // SMIL first-interval filtering excludes animations already ended when the parent begins.
        if (end <= 0 && (begin != 0 || end != 0)) continue;
        if (resolved && !inactive && !never) {
            intervals[resolved-1].end = fmin(intervals[resolved-1].end, begin);
            if (intervals[resolved-1].end <= 0 && intervals[resolved-1].begin != 0) resolved--;
        }
        SvgAnimationInterval* interval = &intervals[resolved++];
        interval->begin = begin; interval->end = end;
        interval->duration = duration; interval->repeating_duration = repeating;
    }
    return resolved;
}

static SvgAnimationTimingEntry* svg_animation_timing_entry(SvgTimeline* timeline,
    DomElement* animation, double horizon, SvgAnimationTimingQuery* query, unsigned depth) {
    SvgAnimationTimingEntry* entry = nullptr;
    for (SvgAnimationTimingEntry* candidate : query->entries) {
        if (!query->remaining) { query->failed = true; return nullptr; }
        query->remaining--;
        if (candidate->element == animation) { entry = candidate; break; }
    }
    if (!entry) {
        if (query->entries.size() >= 512) { query->failed = true; return nullptr; }
        entry = (SvgAnimationTimingEntry*)mem_calloc(1, sizeof(*entry), MEM_CAT_RENDER); // OBJ_HEAP_OK: the timing query's entry list owns it until the query ends
        if (!entry) { query->failed = true; return nullptr; }
        entry->element = animation; entry->horizon = horizon;
        if (!query->entries.append(entry)) { lam::Temp<SvgAnimationTimingEntry> dropped(entry); query->failed = true; return nullptr; }
        query->changed = true;
    }
    // an anchored cycle borrows the last iteration; an unanchored cycle starts empty.
    if (entry->evaluating) return entry;
    if (horizon > entry->horizon) { entry->horizon = horizon; query->changed = true; entry->pass = 0; }
    if (entry->pass == query->pass) return entry;
    if (depth >= 32 || !isfinite(entry->horizon)) { query->failed = true; return nullptr; }
    entry->evaluating = true;
    SvgAnimationInterval intervals[256];
    unsigned count = svg_animation_calculate_intervals(timeline, animation, intervals, 256,
        query, entry->horizon, depth, entry);
    entry->evaluating = false; entry->pass = query->pass;
    if (entry->count != count || memcmp(entry->intervals, intervals, count * sizeof(*intervals)) != 0) {
        memcpy(entry->intervals, intervals, count * sizeof(*intervals));
        entry->count = count; query->changed = true;
    }
    return entry;
}

static unsigned svg_animation_intervals(SvgTimeline* timeline, DomElement* animation,
    SvgAnimationInterval* intervals, unsigned capacity, double horizon = NAN) {
    SvgAnimationTimingQuery query;
    SvgAnimationTimingEntry* entry = nullptr;
    do {
        query.changed = false; query.pass++;
        entry = svg_animation_timing_entry(timeline, animation, isnan(horizon) ? timeline->time : horizon, &query, 0);
    } while (query.changed && !query.failed && query.remaining && query.pass < 256);
    if (query.failed || (query.changed && (!query.remaining || query.pass == 256))) {
        if (!svg_animation_work || !svg_animation_work->reported) {
            log_error("SVG_ANIMATION_TIMING_LIMIT: syncbase evaluation exceeds its bounded work budget");
            if (svg_animation_work) svg_animation_work->reported = true;
        }
        if (timeline->registry->sampling) timeline->registry->sample_failed = true;
        return 0;
    }
    unsigned count = entry ? (entry->count < capacity ? entry->count : capacity) : 0;
    if (count) memcpy(intervals, entry->intervals, count * sizeof(*intervals));
    return count;
}

bool svg_animation_start_time(DomElement* animation, double* seconds) {
    if (!animation || !svg_animation_is_element(animation) || !seconds) return false;
    SvgTimeline* timeline = svg_animation_timeline(animation, true);
    if (!timeline) return false;
    SvgAnimationInterval intervals[256];
    // DOM queries include resolved future intervals; paint samples only evaluate through the current time.
    unsigned count = svg_animation_intervals(timeline, animation, intervals, 256, DBL_MAX / 2);
    for (unsigned i = 0; i < count; i++) {
        if (intervals[i].end > timeline->time) { *seconds = intervals[i].begin; return true; }
    }
    return false;
}

double svg_animation_simple_duration(DomElement* animation) {
    if (!animation || !svg_animation_is_element(animation)) return NAN;
    double duration = svg_animation_clock_value(animation->get_attribute("dur"), NAN);
    return duration > 0 && isfinite(duration) ? duration : NAN;
}

static bool svg_animation_interval(SvgTimeline* timeline, DomElement* animation,
    SvgAnimationInterval* interval) {
    SvgAnimationInterval intervals[256];
    unsigned count = svg_animation_intervals(timeline, animation, intervals, 256);
    bool resolved = false;
    for (unsigned i = 0; i < count && intervals[i].begin <= timeline->time; i++) {
        *interval = intervals[i]; resolved = true;
    }
    return resolved;
}

bool svg_animation_begin_end(DomElement* animation, bool end, double offset) {
    if (!animation || !svg_animation_is_element(animation) || !isfinite(offset) ||
        !svg_animation_connected(animation->doc, dom_node_ref(animation))) return false;
    SvgTimeline* timeline = svg_animation_timeline(animation, true);
    if (!timeline) return false;
    SvgAnimationInterval interval = {};
    if (end && (!svg_animation_interval(timeline, animation, &interval) || timeline->time >= interval.end)) return false;
    SvgAnimationControl* control = svg_animation_control(timeline->registry, animation, true);
    if (!control) return false;
    for (SvgAnimationInstanceTime* time = control->times; time; time = time->next)
        if (time->value == timeline->time + offset && time->resolved_at == timeline->time &&
            time->end == end && time->broadcast == svg_animation_event_broadcast) return true;
    if (control->count >= 256 || timeline->registry->budget_owner->total_instance_count >= 65536) return false;
    SvgAnimationInstanceTime* time = (SvgAnimationInstanceTime*)mem_calloc(1, sizeof(*time), MEM_CAT_RENDER); // OBJ_HEAP_OK: the animation control's instance-time list owns it
    if (!time) return false;
    time->value = timeline->time + offset; time->resolved_at = timeline->time; time->end = end;
    time->broadcast = svg_animation_event_broadcast;
    time->next = control->times; control->times = time; control->count++;
    timeline->registry->instance_count++;
    timeline->registry->budget_owner->total_instance_count++;
    timeline->registry->generation++;
    if (timeline->registry->owner_document->state) doc_state_request_repaint(timeline->registry->owner_document->state);
    if (!timeline->paused && animation->doc->js.host_ui_context) svg_animation_start_driver(timeline);
    return true;
}

static unsigned svg_animation_event_offsets(DomElement* animation, const char* text,
    DomElement* source, const char* type, bool bubbles, double detail, const char* key,
    double* offsets, bool* broadcasts, unsigned capacity, bool explicit_only) {
    StrSplitIter split;
    if (!svg_animation_list_begin(&split, text)) return 0;
    const char* start = nullptr; size_t length = 0;
    unsigned count = 0;
    while (svg_animation_list_next(&split, &start, &length)) {
        if (!length || length >= 1024) continue;
        char token[1024]; memcpy(token, start, length); token[length] = 0;
        if (key) {
            if (strncmp(token, "accessKey(", 10) != 0) continue;
            const char* character = token + 10; uint32_t codepoint;
            int bytes = str_utf8_decode(character, strlen(character), &codepoint);
            if (bytes <= 0 || character[bytes] != ')' || strlen(key) != (size_t)bytes ||
                memcmp(character, key, bytes) != 0) continue;
            const char* suffix = str_skip_ascii_space(character + bytes + 1);
            double offset = !*suffix ? 0 : (*suffix == '+' || *suffix == '-')
                ? svg_animation_clock_value(suffix, NAN) : NAN;
            if (isfinite(offset) && !svg_animation_insert_time(offsets, &count, capacity, offset)) {
                log_error("SVG_ANIMATION_TIMING_LIMIT: access key exceeds 256 distinct offsets");
                return 0;
            }
            for (unsigned i = 0; i < count; i++) broadcasts[i] = true;
            continue;
        }
        SvgAnimationReference reference = {};
        if (!svg_animation_reference(animation, token, &reference)) continue;
        if (explicit_only && !reference.explicit_id) continue;
        bool matches = strcmp(reference.symbol, type) == 0;
        if (!matches && strcmp(type, "repeatEvent") == 0) {
            matches = strcmp(reference.symbol, "repeat") == 0;
            if (strncmp(reference.symbol, "repeat(", 7) == 0) {
                char* end = nullptr;
                double iteration = strtod(reference.symbol + 7, &end);
                matches = end != reference.symbol + 7 && *end == ')' && !end[1] &&
                    iteration >= 0 && iteration == floor(iteration) && iteration == detail;
            }
        }
        if (!matches) continue;
        for (DomNode* node = source; node; node = bubbles ? node->parent : nullptr)
            if (node == reference.element) {
                unsigned position = 0;
                while (position < count && offsets[position] < reference.offset) position++;
                bool exists = position < count && offsets[position] == reference.offset;
                if (!exists && count < capacity)
                    for (unsigned i = count; i > position; i--) broadcasts[i] = broadcasts[i-1];
                if (!svg_animation_insert_time(offsets, &count, capacity, reference.offset)) {
                    log_error("SVG_ANIMATION_TIMING_LIMIT: event exceeds 256 distinct offsets");
                    return 0;
                }
                broadcasts[position] = (exists && broadcasts[position]) || reference.explicit_id;
                break;
            }
    }
    return count;
}

static void svg_animation_event_tree(SvgTimeline* timeline, DomElement* node,
    DomElement* source, const char* type, bool bubbles, double detail, const char* key,
    unsigned depth, size_t* remaining, bool explicit_only = false) {
    if (!node || depth >= 256 || !*remaining) return;
    if (svg_animation_separate_fragment(node, timeline->root.address)) return;
    (*remaining)--;
    if (svg_animation_is_element(node)) {
        double begin_offsets[256], end_offsets[256];
        bool begin_broadcasts[256] = {}, end_broadcasts[256] = {};
        unsigned begin = svg_animation_event_offsets(node, node->get_attribute("begin"), source, type, bubbles, detail, key, begin_offsets, begin_broadcasts, 256, explicit_only);
        unsigned end = svg_animation_event_offsets(node, node->get_attribute("end"), source, type, bubbles, detail, key, end_offsets, end_broadcasts, 256, explicit_only);
        if (begin || end) {
            SvgAnimationInterval interval = {};
            bool resolved = svg_animation_interval(timeline, node, &interval);
            bool active = resolved && timeline->time < interval.end;
            const char* restart = node->get_attribute("restart");
            bool never = svg_animation_keyword(restart, "never");
            bool inactive = svg_animation_keyword(restart, "whenNotActive");
            // one event occurrence resolves either begin or end, according to SMIL event sensitivity.
            bool saved_broadcast = svg_animation_event_broadcast;
            if (begin && !(never && resolved) && !(inactive && active)) {
                for (unsigned i = 0; i < begin; i++) {
                    svg_animation_event_broadcast = begin_broadcasts[i];
                    svg_animation_begin_end(node, false, begin_offsets[i]);
                }
            } else if (end && active) {
                for (unsigned i = 0; i < end; i++) {
                    svg_animation_event_broadcast = end_broadcasts[i];
                    svg_animation_begin_end(node, true, end_offsets[i]);
                }
            }
            svg_animation_event_broadcast = saved_broadcast;
        }
    }
    for (DomNode* child = node->first_child; child; child = child->next_sibling)
        if (child->is_element()) svg_animation_event_tree(timeline, child->as_element(), source, type, bubbles, detail, key, depth + 1, remaining, explicit_only);
}

static void svg_animation_dispatch_event(DomElement* target, const char* type, bool bubbles, double detail, const char* key) {
    if (!target || !target->doc || !type || target->doc->services.svg_image_document) return;
    SvgAnimationRegistry* registry = svg_animation_registry(target->doc, false);
    if (!registry) return;
    SvgAnimationWorkScope work(registry);
    for (SvgTimeline* timeline = registry->timelines; timeline; timeline = timeline->next) {
        DomNode* root = dom_node_ref_validate(target->doc, timeline->root);
        if (!root || !root->is_element() || !svg_animation_connected(target->doc, timeline->root)) continue;
        if (registry->instance_root.address)
            root = dom_node_ref_validate(target->doc, registry->instance_root);
        if (!root || !root->is_element()) continue;
        size_t remaining = 65536;
        double previous = timeline->time;
        if (!timeline->paused && timeline->driver)
            timeline->time = timeline->origin + target->doc->state->animation_scheduler->current_time - timeline->driver->start_time;
        if (!timeline->paused) timeline->time += registry->event_time_offset;
        svg_animation_event_tree(timeline, root->as_element(), target, type, bubbles, detail, key, 0, &remaining);
        timeline->time = previous;
    }
}

extern "C" void dom_engine_svg_timing_event(DomElement* target, const char* type, bool bubbles, double detail) {
    svg_animation_dispatch_event(target, type, bubbles, detail, nullptr);
}

static void svg_animation_broadcast_use_event(SvgTimeline* host_timeline, DomElement* source,
    const char* type, bool bubbles, double detail, const char* key) {
    SvgAnimationRegistry* saved = svg_animation_instance_registry;
    svg_animation_instance_registry = nullptr;
    SvgTimeline* original = svg_animation_timeline(source, true);
    if (original && host_timeline) {
        double previous = original->time;
        original->time = host_timeline->time;
        size_t remaining = 65536;
        DomNode* root = dom_node_ref_validate(source->doc, original->root);
        // SVG 2 §5.6.5: ID-qualified events and access keys also initialize later instances.
        if (root && root->is_element())
            svg_animation_event_tree(original, root->as_element(), source, type, bubbles, detail,
                key, 0, &remaining, true);
        original->time = previous;
    }
    svg_animation_instance_registry = saved;
}

static void svg_animation_key_instances(SvgAnimationRegistry* owner, const char* key, unsigned depth) {
    if (!owner || depth >= 32) return;
    for (SvgAnimationUseInstance* instance = owner->instances; instance; instance = instance->next) {
        DomNode* host = dom_node_ref_validate(owner->document, instance->host);
        DomNode* source = dom_node_ref_validate(instance->registry->document, instance->source);
        if (!host || !source || !host->is_element() || !source->is_element() ||
            !svg_animation_connected(owner->document, instance->host)) continue;
        svg_animation_broadcast_use_event(svg_animation_timeline(host->as_element(), true),
            source->as_element(), "keydown", false, 0, key);
        SvgAnimationSourceScope scope(source->as_element()->doc, source->as_element(), host->as_element());
        svg_animation_dispatch_event(source->as_element(), "keydown", false, 0, key);
        svg_animation_key_instances(instance->registry, key, depth + 1);
    }
}

extern "C" void dom_engine_svg_timing_key(DomElement* target, const char* key) {
    if (!target || !key || !str_utf8_valid(key, strlen(key)) || str_utf8_count(key, strlen(key)) != 1) return;
    SvgAnimationRegistry* owner = svg_animation_registry(target->doc, false);
    if (!owner) return;
    SvgAnimationWorkScope work(owner);
    svg_animation_dispatch_event(target, "keydown", false, 0, key);
    svg_animation_key_instances(owner, key, 0);
    owner->budget_owner->generation++;
}

void svg_animation_use_event(DomElement* host, DomElement* source, const char* type, bool bubbles, double detail) {
    if (!host || !source || !type || host->doc->services.svg_image_document) return;
    SvgAnimationRegistry* owner = svg_animation_registry(host->doc, false);
    SvgAnimationUseInstance* instance = owner ? owner->instances : nullptr;
    while (instance && (instance->host.address != host || instance->host.expected_id != host->DomNode::id))
        instance = instance->next;
    DomNode* root = instance ? dom_node_ref_validate(source->doc, instance->source) : nullptr;
    if (!root || !root->is_element()) return;
    svg_animation_broadcast_use_event(svg_animation_timeline(host, true), source, type, bubbles, detail, nullptr);
    SvgAnimationSourceScope scope(source->doc, root->as_element(), host);
    svg_animation_dispatch_event(source, type, bubbles, detail, nullptr);
    owner->generation++;
    if (owner != owner->budget_owner) owner->budget_owner->generation++;
    if (owner->owner_document->state) doc_state_request_repaint(owner->owner_document->state);
}

static void svg_animation_forget_source_registry(SvgAnimationRegistry* owner, DomDocument* source) {
    if (!owner) return;
    for (SvgAnimationUseInstance** link = &owner->instances; *link;) {
        SvgAnimationUseInstance* instance = *link;
        if (instance->registry->document == source) {
            *link = instance->next;
            owner->budget_owner->total_use_count--;
            svg_animation_registry_destroy(instance->registry); lam::Temp<SvgAnimationUseInstance> owned(instance);
        } else {
            svg_animation_forget_source_registry(instance->registry, source);
            link = &instance->next;
        }
    }
}

void svg_animation_forget_source_document(DomDocument* host, DomDocument* source) {
    svg_animation_forget_source_registry(host ? (SvgAnimationRegistry*)host->services.svg_animation_registry : nullptr, source);
}

DomElement* svg_animation_use_source(DomElement* host) {
    SvgAnimationRegistry* owner = host ? svg_animation_registry(host->doc, false) : nullptr;
    for (SvgAnimationUseInstance* instance = owner ? owner->instances : nullptr; instance; instance = instance->next)
        if (instance->host.address == host && instance->host.expected_id == host->DomNode::id) {
            DomNode* source = dom_node_ref_validate(instance->registry->document, instance->source);
            return source ? source->as_element() : nullptr;
        }
    return nullptr;
}

struct SvgAnimationNotification {
    DomNodeRef target;
    double time;
    double detail;
    unsigned kind;
};

typedef lam::ArrayList<SvgAnimationNotification> SvgAnimationNotifications;

static void svg_animation_queue_notification(DomElement* animation,
    SvgAnimationNotifications* events, double time, double detail, unsigned kind) {
    if (events->size() >= 1024 || !isfinite(time)) return;
    SvgAnimationNotification event = {dom_node_ref(animation), time, detail, kind};
    events->append(event);
}

static void svg_animation_collect_notifications(SvgTimeline* timeline, DomElement* node,
    double previous, SvgAnimationNotifications* events, unsigned depth, size_t* remaining) {
    if (!node || depth >= 256 || !*remaining) return;
    if (svg_animation_separate_fragment(node, timeline->root.address)) return;
    (*remaining)--;
    if (svg_animation_is_element(node) && dom_svg_element_is_eligible(node)) {
        SvgAnimationInterval intervals[256];
        unsigned count = svg_animation_intervals(timeline, node, intervals, 256);
        SvgAnimationControl* control = svg_animation_control(timeline->registry, node, true);
        if (control) for (unsigned i = 0; i < count && intervals[i].begin <= timeline->time; i++) {
            SvgAnimationInterval* interval = &intervals[i];
            if (interval->end < previous) continue;
            if (!control->begin_notified || control->notified_begin != interval->begin) {
                control->notified_begin = interval->begin; control->begin_notified = true;
                control->end_notified = false; control->notified_repeat = 0;
                if (interval->begin >= previous)
                    svg_animation_queue_notification(node, events, interval->begin, 0, 0);
            }
            if (isfinite(interval->duration)) {
                double first = fmax(1, floor((previous - interval->begin) / interval->duration));
                double last = floor((timeline->time - interval->begin) / interval->duration);
                unsigned queued = 0;
                for (double repeat = first; repeat <= last && queued < 1024; repeat++) {
                    double time = interval->begin + repeat * interval->duration;
                    if (time >= interval->end || time > timeline->time) break;
                    if (time < previous || repeat <= control->notified_repeat) continue;
                    control->notified_repeat = repeat; queued++;
                    svg_animation_queue_notification(node, events, time, repeat, 1);
                }
            }
            if (interval->end >= previous && interval->end <= timeline->time &&
                (!control->end_notified || control->notified_end != interval->end)) {
                control->notified_end = interval->end; control->end_notified = true;
                svg_animation_queue_notification(node, events, interval->end, 0, 2);
            }
        }
    }
    for (DomNode* child = node->first_child; child; child = child->next_sibling)
        if (child->is_element()) svg_animation_collect_notifications(timeline, child->as_element(),
            previous, events, depth + 1, remaining);
}

static void svg_animation_notify_time(SvgTimeline* timeline, double previous, unsigned depth) {
    if (depth >= 32) return;
    if (timeline->time < previous) return;
    DomDocument* doc = timeline->registry->document;
    DomNode* root = dom_node_ref_validate(doc, timeline->registry->instance_root.address
        ? timeline->registry->instance_root : timeline->root);
    if (!root || !root->is_element()) return;
    SvgAnimationWorkScope work(timeline->registry);
    SvgAnimationNotifications events(MEM_CAT_RENDER, 16);
    size_t remaining = 65536;
    svg_animation_collect_notifications(timeline, root->as_element(), previous, &events, 0, &remaining);
    // DOM callbacks can replace or retire animation nodes; collect values before entering JS.
    for (size_t i = 1; i < events.size(); i++) {
        SvgAnimationNotification event = events[i]; size_t position = i;
        while (position && events[position-1].time > event.time) {
            events[position] = events[position-1]; position--;
        }
        events[position] = event;
    }
    static const char* names[] = {"beginEvent", "repeatEvent", "endEvent"};
    for (size_t i = 0; i < events.size(); i++) {
        if (!svg_animation_connected(doc, timeline->root)) break;
        SvgAnimationNotification* event = &events[i];
        DomNode* target = dom_node_ref_validate(doc, event->target);
        if (!target || !target->is_element() || !svg_animation_connected(doc, event->target)) continue;
        double saved_offset = timeline->registry->event_time_offset;
        // Syncbase/event dependents use the defined boundary even when this frame arrives later.
        timeline->registry->event_time_offset = event->time - timeline->time;
        if (timeline->registry->instance_host.address) {
            double current = timeline->time;
            timeline->time = event->time;
            timeline->registry->event_time_offset = 0;
            // shadow animation nodes have no public DOM wrapper; their timing events still drive dependents.
            svg_animation_broadcast_use_event(timeline, target->as_element(), names[event->kind], false, event->detail, nullptr);
            svg_animation_dispatch_event(target->as_element(), names[event->kind], false, event->detail, nullptr);
            timeline->time = current;
        } else {
            radiant_dispatch_svg_time_event((UiContext*)doc->js.host_ui_context,
                target->as_element(), names[event->kind], event->detail, event->time);
        }
        timeline->registry->event_time_offset = saved_offset;
    }
    for (SvgAnimationUseInstance* instance = timeline->registry->instances; instance; instance = instance->next) {
        DomNode* host = dom_node_ref_validate(doc, instance->host);
        DomNode* source = dom_node_ref_validate(instance->registry->document, instance->source);
        if (!host || !source || !host->is_element() || !source->is_element() ||
            !svg_animation_connected(doc, instance->host) || svg_animation_root(host->as_element()) != timeline->root.address) continue;
        SvgAnimationSourceScope scope(source->as_element()->doc, source->as_element(), host->as_element());
        SvgTimeline* shadow = svg_animation_timeline(source->as_element(), true);
        if (shadow) svg_animation_notify_time(shadow, previous, depth + 1);
    }
}

static void svg_animation_sample_node(SvgTimeline* timeline, DomElement* animation,
    const SvgAnimationInterval* sampled_interval, size_t order);

static const char* svg_animation_frozen_sample(SvgTimeline* timeline, DomElement* animation,
    DomElement* target, const char* name, bool css, const SvgAnimationInterval* interval,
    size_t order, double freeze_time, float progress, double iteration);

static uint64_t svg_animation_signature(DomElement* animation) {
    static const char* names[] = {"attributeName", "attributeType", "from", "to", "by", "values",
        "keyTimes", "keySplines", "calcMode", "additive", "accumulate", "type", "fill", "dur",
        "repeatCount", "repeatDur", "min", "max", "begin", "end", "href", "xlink:href"};
    uint64_t signature = 0;
    for (const char* name : names) {
        const char* value = animation->get_attribute(name);
        if (value) signature = hashmap_xxhash3(value, strlen(value), signature, 1);
        signature = hashmap_xxhash3(name, strlen(name), signature, value != nullptr);
    }
    return signature;
}

static void svg_animation_sample_node(SvgTimeline* timeline, DomElement* animation,
    const SvgAnimationInterval* sampled_interval, size_t order) {
    SvgAnimationRegistry* registry = timeline->registry;
    const char* name = svg_animation_attribute_name(animation);
    DomElement* target = svg_animation_target_element(animation);
    if (!target || !name || !dom_element_is_svg(target) || !dom_svg_element_is_eligible(animation)) return;
    if (svg_animation_value_class(target, name) == SVG_ANIMATION_UNSUPPORTED) return;
    bool transform = svg_animation_name_in("transform gradientTransform patternTransform", name);
    if ((strcmp(animation->local_name(), "animateTransform") == 0 && !transform) ||
        (strcmp(animation->local_name(), "animate") == 0 && transform)) return;
    SvgAnimationInterval interval = *sampled_interval;
    double duration = interval.duration;
    double elapsed = timeline->time - interval.begin;
    double effect_duration = fmin(interval.end - interval.begin, interval.repeating_duration);
    bool terminal = elapsed >= effect_duration;
    const char* fill = animation->get_attribute("fill");
    if (terminal && !svg_animation_keyword(fill, "freeze")) return;
    if (terminal) elapsed = effect_duration;
    double cycles = isfinite(duration) ? elapsed / duration : 0;
    double iteration = floor(cycles);
    float progress = cycles - iteration;
    double integral = round(cycles);
    // decimal durations can leave a tiny fmod remainder at an exact repeat-count end.
    if (terminal && isfinite(duration) && elapsed > 0 &&
        fabs(cycles - integral) <= 8 * DBL_EPSILON * fmax(1, fabs(cycles))) {
        progress = 1.0f; iteration = fmax(0, integral - 1);
    }
    const char* attribute_type = animation->get_attribute("attributeType");
    if (attribute_type && !svg_animation_keyword(attribute_type, "XML") &&
        !svg_animation_keyword(attribute_type, "CSS") && !svg_animation_keyword(attribute_type, "auto")) return;
    bool css = svg_animation_keyword(attribute_type, "XML") ? false :
        svg_animation_keyword(attribute_type, "CSS") ? true : css_property_get_by_name(name) != nullptr;
    const char* to = animation->get_attribute("to");
    const char* result = nullptr;
    SvgAnimationControl* control = svg_animation_control(registry, animation, false);
    bool frozen_to = terminal && to && !animation->get_attribute("from") && !animation->get_attribute("values") &&
        strcmp(animation->local_name(), "set") != 0;
    if (!frozen_to) svg_animation_clear_frozen(registry, control);
    if (strcmp(animation->local_name(), "set") == 0) {
        to = svg_animation_token(registry, to);
        SvgAnimationVector vector = {};
        bool numeric = svg_animation_vector(registry, target, name, to, nullptr, &vector);
        if (svg_animation_value_valid(target, name, to, numeric)) result = svg_animation_copy(registry, to);
    } else if (frozen_to) {
        uint64_t signature = svg_animation_signature(animation);
        if (control && control->frozen_text && control->frozen_signature == signature &&
            control->frozen_begin == interval.begin && control->frozen_end == interval.end &&
            dom_node_ref_validate(registry->document, control->frozen_target) == target)
            result = svg_animation_copy(registry, control->frozen_text);
        else {
            result = svg_animation_frozen_sample(timeline, animation, target, name, css, &interval,
                order, interval.begin + effect_duration, progress, iteration);
            // nested evaluation can grow the control map; reacquire its entry before publishing.
            control = svg_animation_control(registry, animation, true);
            if (result && control) {
                svg_animation_clear_frozen(registry, control);
                size_t bytes = strlen(result) + 1;
                if (bytes > 8u * 1024u * 1024u - registry->budget_owner->total_frozen_bytes) {
                    log_error("SVG_ANIMATION_LIMIT: frozen value storage exceeds 8 MiB");
                    registry->sample_failed = true;
                } else {
                    control->frozen_text = mem_strdup(result, MEM_CAT_RENDER);
                    if (!control->frozen_text) registry->sample_failed = true;
                    else {
                        registry->frozen_bytes += bytes;
                        registry->budget_owner->total_frozen_bytes += bytes;
                        control->frozen_signature = signature; control->frozen_target = dom_node_ref(target);
                        control->frozen_begin = interval.begin; control->frozen_end = interval.end;
                    }
                }
            }
        }
    } else result = svg_animation_sample_values(timeline, animation, target, name, css, progress, iteration, duration);
    if (result) svg_animation_store(registry, target, name, result, css);
}

struct SvgAnimationCandidate {
    DomElement* animation;
    SvgAnimationInterval interval;
    size_t order;
    DomElement* target;
    unsigned dependency_phase;
    unsigned target_depth;
};

typedef lam::ArrayList<SvgAnimationCandidate> SvgAnimationCandidates;

static void svg_animation_candidate_dependencies(SvgAnimationCandidate* candidate) {
    candidate->target = svg_animation_target_element(candidate->animation);
    const char* name = svg_animation_attribute_name(candidate->animation);
    candidate->dependency_phase = name && svg_animation_name_in("font-family font-size font-weight font-style", name) ? 0 :
        name && candidate->target && candidate->target->tag() == MARKUP_NAME_SVG &&
            svg_animation_name_in("viewBox width height", name) ? 1 : name && strcmp(name, "color") == 0 ? 2 :
        name && strcmp(name, "type") == 0 ? 3 : 4;
    if (candidate->dependency_phase < 3)
        for (DomNode* parent = candidate->target; parent; parent = parent->parent) candidate->target_depth++;
}

static int svg_animation_priority_compare(const SvgAnimationCandidate* a, const SvgAnimationCandidate* b) {
    if (a->interval.begin != b->interval.begin) return a->interval.begin < b->interval.begin ? -1 : 1;
    return a->order == b->order ? 0 : a->order < b->order ? -1 : 1;
}

static int svg_animation_candidate_compare(const void* left, const void* right) {
    const SvgAnimationCandidate* a = (const SvgAnimationCandidate*)left;
    const SvgAnimationCandidate* b = (const SvgAnimationCandidate*)right;
    // font, viewport, currentColor and filter type samples are inputs to other attribute functions.
    if (a->dependency_phase != b->dependency_phase) return a->dependency_phase < b->dependency_phase ? -1 : 1;
    if (a->target_depth != b->target_depth) return a->target_depth < b->target_depth ? -1 : 1;
    return svg_animation_priority_compare(a, b);
}

static void svg_animation_sample_tree(SvgTimeline* timeline, DomElement* node,
    SvgAnimationCandidates* candidates, unsigned depth, size_t* remaining) {
    if (!node || timeline->registry->sample_failed) return;
    if (svg_animation_separate_fragment(node, timeline->root.address)) return;
    if (depth >= 256 || !*remaining) {
        log_error("SVG_ANIMATION_LIMIT: sample traversal exceeds 65536 nodes or 256 levels");
        timeline->registry->sample_failed = true; return;
    }
    (*remaining)--;
    if (svg_animation_is_element(node)) {
        SvgAnimationCandidate candidate = {};
        candidate.animation = node; candidate.order = 65536 - *remaining;
        svg_animation_candidate_dependencies(&candidate);
        if (svg_animation_interval(timeline, node, &candidate.interval) && !candidates->append(candidate))
            timeline->registry->sample_failed = true;
    }
    for (DomNode* child = node->first_child; child; child = child->next_sibling)
        if (child->is_element()) svg_animation_sample_tree(timeline, child->as_element(), candidates, depth + 1, remaining);
}

static const char* svg_animation_frozen_sample(SvgTimeline* timeline, DomElement* animation,
    DomElement* target, const char* name, bool css, const SvgAnimationInterval* interval,
    size_t order, double freeze_time, float progress, double iteration) {
    SvgAnimationRegistry* registry = timeline->registry;
    if (registry->frozen_depth >= 32) {
        log_error("SVG_ANIMATION_TIMING_LIMIT: frozen sandwich exceeds 32 dependent levels");
        registry->sample_failed = true; return nullptr;
    }
    DomNode* root = dom_node_ref_validate(registry->document, timeline->root);
    HashMap* temporary = root ? SvgAnimatedTargetMap::create(32) : nullptr;
    if (!temporary || !root->is_element()) {
        if (root && !temporary) registry->sample_failed = true;
        SvgAnimatedTargetMap::destroy(temporary); return nullptr;
    }
    registry->frozen_depth++;
    double saved_time = timeline->time; HashMap* saved_targets = registry->targets;
    timeline->time = freeze_time; registry->targets = temporary;
    SvgAnimationCandidates candidates(MEM_CAT_RENDER, 0);
    size_t remaining = 65536;
    svg_animation_sample_tree(timeline, root->as_element(), &candidates, 0, &remaining);
    if (candidates.size() > 1) qsort(candidates.data(), candidates.size(),
        sizeof(SvgAnimationCandidate), svg_animation_candidate_compare);
    SvgAnimationCandidate ceiling = {animation, *interval, order, target, 0, 0};
    svg_animation_candidate_dependencies(&ceiling);
    for (size_t i = 0; i < candidates.size() && !registry->sample_failed; i++) {
        // dependency order includes inherited inputs while keeping frozen sandwiches acyclic.
        if (svg_animation_candidate_compare(&candidates[i], &ceiling) >= 0) break;
        svg_animation_sample_node(timeline, candidates[i].animation, &candidates[i].interval, candidates[i].order);
    }
    // SMIL freezes the complete to-function at the active end, including its underlying sandwich.
    const char* result = registry->sample_failed ? nullptr :
        svg_animation_sample_values(timeline, animation, target, name, css, progress, iteration, interval->duration);
    timeline->time = saved_time; registry->targets = saved_targets;
    SvgAnimatedTargetMap::destroy(temporary); registry->frozen_depth--;
    return result;
}

static void svg_animation_sample(SvgAnimationRegistry* registry) {
    if (registry->sampling || (registry->sampled_generation == registry->generation &&
        registry->sampled_epoch == registry->document->mutation_epoch)) return;
    svg_animation_prune(registry);
    SvgAnimationWorkScope work(registry);
    registry->sampling = true;
    registry->budget_owner->total_sample_bytes -= registry->sample_bytes;
    registry->sample_bytes = 0; registry->sample_failed = false;
    hashmap_clear(registry->targets, false);
    if (registry->samples) mem_pool_destroy(registry->samples);
    registry->samples = mem_pool_create((MemContext*)registry->owner_document->services.mem_ctx,
        MEM_ROLE_RENDER, "svg.animation.samples");
    if (registry->samples) for (SvgTimeline* timeline = registry->timelines; timeline; timeline = timeline->next) {
        DomNode* root = dom_node_ref_validate(registry->document,
            registry->instance_host.address ? registry->instance_root : timeline->root);
        if (root && root->is_element() && svg_animation_connected(registry->document, timeline->root)) {
            size_t remaining = 65536;
            SvgAnimationCandidates candidates(MEM_CAT_RENDER, 0);
            svg_animation_sample_tree(timeline, root->as_element(), &candidates, 0, &remaining);
            // SVG's sandwich priority follows activation time, with document order breaking ties.
            if (candidates.size() > 1) qsort(candidates.data(), candidates.size(),
                sizeof(SvgAnimationCandidate), svg_animation_candidate_compare);
            for (size_t i = 0; i < candidates.size() && !registry->sample_failed; i++)
                svg_animation_sample_node(timeline, candidates[i].animation, &candidates[i].interval, candidates[i].order);
        }
    }
    if (registry->samples && !registry->sample_failed) {
        registry->sampled_epoch = registry->document->mutation_epoch;
        registry->sampled_generation = registry->generation;
    } else hashmap_clear(registry->targets, false);
    registry->sampling = false;
}

static bool svg_animation_node_visible(SvgAnimationRegistry* registry, DomNode* node) {
    if (!node) return false;
    if (registry->reference_document || registry->instance_host.address) {
        if (svg_animation_source_document != registry->document || !svg_animation_source_instance) return false;
        while (node && node != svg_animation_source_instance) node = node->parent;
        if (!node) return false;
    }
    return true;
}

uint64_t svg_animation_source_generation(DomDocument* document, Element* root, DomElement* node) {
    SvgAnimationRegistry* registry = svg_animation_registry(document, false);
    if (!registry) return 0;
    if (registry->instance_host.address) return hashmap_hash_pointer_identity(registry, registry->generation, registry->original_generation);
    if (!registry->reference_document) return registry->generation;
    // shared external resources stay static outside the active use subtree (SVG 2 §5.6.5).
    if (!node) node = dom_find_element_for_source(document->root, root);
    return svg_animation_node_visible(registry, node) ? registry->generation : 0;
}

static bool svg_animation_target_visible(SvgAnimationRegistry* registry, const SvgAnimatedTarget* entry) {
    return entry && svg_animation_node_visible(registry, dom_node_ref_validate(registry->document, entry->element));
}

static const char* svg_animation_target_value(SvgAnimationRegistry* registry, Element* source,
    const char* name, unsigned type) {
    if (!registry) return nullptr;
    if (!registry->sampling) svg_animation_sample(registry);
    SvgAnimatedTarget key = {}; key.source = source;
    SvgAnimatedTarget* entry = SvgAnimatedTargetMap::get(registry->targets, key);
    if (!svg_animation_target_visible(registry, entry)) return nullptr;
    for (SvgAnimatedValue* value = entry->values; value; value = value->next)
        if ((!type || value->css == (type == 1)) && strcmp(value->name, name) == 0) return value->text;
    return nullptr;
}

const char* svg_animation_value(DomElement* element, const char* name, bool css_only) {
    return element ? svg_animation_target_value(svg_animation_registry(element->doc, false),
        dom_element_to_element(element), name, css_only ? 1 : 0) : nullptr;
}



const char* svg_animation_attribute(DomElement* element, const char* name) {
    const char* value = element ? svg_animation_target_value(svg_animation_registry(element->doc, false),
        dom_element_to_element(element), name, 2) : nullptr;
    return value ? value : element ? element->get_attribute(name) : nullptr;
}

const char* svg_animation_source_value(Element* element, const char* name) {
    if (!element || !name) return nullptr;
    SvgAnimationRegistry* registry = svg_animation_registry(svg_animation_source_document, false);
    const char* xml = svg_animation_target_value(registry, element, name, 2);
    if (!xml && strcmp(name, "xlink:href") == 0 && svg_animation_source_document) {
        DomElement* target = dom_find_element_for_source(svg_animation_source_document->root, element);
        return dom_element_attribute_ns(target, "http://www.w3.org/1999/xlink", "href");
    }
    if (!registry || !svg_animation_name_in("x y width height cx cy r rx ry", name)) return xml;
    SvgAnimatedTarget key = {}; key.source = element;
    SvgAnimatedTarget* entry = SvgAnimatedTargetMap::get(registry->targets, key);
    if (!svg_animation_target_visible(registry, entry)) return xml;
    DomNode* target = entry ? dom_node_ref_validate(registry->document, entry->element) : nullptr;
    if (!target || !target->is_element()) return xml;
    for (SvgAnimatedValue* value = entry->values; value; value = value->next) {
        if (strcmp(value->name, name) != 0) continue;
        if (!value->resolved) {
            char* owned = nullptr;
            // geometry attributes enter the cascade before CSS animation and importance are applied.
            const char* computed = svg_get_dom_presentation_property(target->as_element(), name, false,
                nullptr, 0, nullptr, &owned);
            value->resolved_text = computed ? svg_animation_copy(registry, computed) : xml;
            value->resolved = true; lam::Temp<char> dropped(owned);
        }
        return value->resolved_text;
    }
    return xml;
}
