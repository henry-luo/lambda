#include "../lambda/io/image_io.h"
#include "event_sim_render.hpp"
#include "event_sim.hpp"
#include "event.hpp"
#include "layout.hpp"
#include "render.hpp"
#include "scene3d.hpp"
#include "scene3d_animation.hpp"
#include "svg_animation.hpp"
#include "../lambda/module/radiant/radiant_webgl_bridge.hpp"
#include "../lambda/dom/dom.h"
#include "../lambda/dom/realm/dom_realm.h"
#include "../lambda/core/mark_reader.hpp"
#include "../lambda/input/input.hpp"
#include "../lambda/format/format.h"
#include "../lambda/runtime/lambda-root-frame.hpp"
#include "../lambda/runtime/runtime-state.h"
#include "../lambda/runtime/transpiler.hpp"
#include "../lambda/runtime/gc/gc_heap.h"
#include "../lambda/js/js_runtime.h"
#include "../lambda/js/js_runtime_state.hpp"
#include "../lambda/js/js_event_loop.h"
#include "../lib/file.h"
#include "../lib/image.h"
#include "../lib/log.h"
#include "../lib/mem.h"
#include "../lib/mem_context.h"
#include "../lib/time_util.h"
#include <cmath>
#include <cstring>

void parse_json(Input* input, const char* source);
extern "C" bool radiant_document_ensure_evaluator(DomDocument* document);

struct EventSimRenderCommand {
    char* json;
    const char* name;
};
struct RenderMetric { const char* name; double value; };
struct RenderMetrics {
    RenderMetric values[48];
    unsigned count;
    void put(const char* name, double value) { if (count < 48) values[count++] = {name, value}; }
    bool get(const char* name, double* value) const {
        for (unsigned i = 0; i < count; i++) if (!strcmp(name, values[i].name)) {
            *value = values[i].value; return true;
        }
        return false;
    }
};
struct RenderCheckpoint {
    RenderCheckpoint* next;
    char* name;
    RenderMetrics metrics;
    ImageSurface* surface;
    DisplayList* display_list;
    lam::Handle<ImageSurface> handle;
    uint8_t* pixels;
    size_t bytes;
};
struct EventSimRenderState { RenderCheckpoint* checkpoints; };

static const char* render_commands[] = {
    "script", "assert_script", "collect_gc", "scene3d", "animation_tick", "animation_frame",
    "snapshot_surface", "release_surface", "assert_surface",
    "snapshot_render_stats", "assert_render_stats", "css_animation", "pointer_event", "prepare_canvas",
    "capture_surface", "measure_frames",
};

bool event_sim_render_recognizes(const char* name) {
    for (const char* command : render_commands) if (!strcmp(name, command)) return true;
    return false;
}

bool event_sim_render_parse(MapReader& arguments, SimEvent* event) {
    const char* name = arguments.get("type").cstring();
    if (!name || !event_sim_render_recognizes(name)) return false;
    if ((!strcmp(name, "script") || !strcmp(name, "assert_script")) &&
        !arguments.get("js").cstring()) return false;
    auto* command = (EventSimRenderCommand*)mem_calloc(1, sizeof(EventSimRenderCommand), MEM_CAT_LAYOUT);
    if (!command) return false;
    // copy fixture arguments: navigation may retire the Input that parsed them (D4.5.1v4).
    Pool* pool = pool_create();
    Item value = {.item = (uint64_t)arguments.container()};
    String* json = format_json(pool, value);
    command->json = json ? mem_strdup(json->chars, MEM_CAT_LAYOUT) : nullptr;
    pool_destroy(pool);
    if (!command->json) { mem_free(command); return false; }
    for (const char* known : render_commands) if (!strcmp(name, known)) command->name = known;
    event->render_command = command;
    event->type = !strncmp(name, "assert_", 7) ? SIM_EVENT_ASSERT_RENDER : SIM_EVENT_RENDER_COMMAND;
    return true;
}

void event_sim_render_event_free(SimEvent* event) {
    if (!event->render_command) return;
    mem_free(event->render_command->json);
    mem_free(event->render_command);
    event->render_command = nullptr;
}

static RenderCheckpoint* checkpoint(EventSimContext* context, const char* name, bool create = false) {
    if (!name || !name[0]) return nullptr;
    if (!context->render_state && create)
        context->render_state = (EventSimRenderState*)mem_calloc(1, sizeof(EventSimRenderState), MEM_CAT_LAYOUT);
    if (!context->render_state) return nullptr;
    for (auto* entry = context->render_state->checkpoints; entry; entry = entry->next)
        if (!strcmp(entry->name, name)) return entry;
    if (!create) return nullptr;
    auto* entry = (RenderCheckpoint*)mem_calloc(1, sizeof(RenderCheckpoint), MEM_CAT_LAYOUT);
    if (!entry) return nullptr;
    entry->name = mem_strdup(name, MEM_CAT_LAYOUT);
    entry->next = context->render_state->checkpoints;
    context->render_state->checkpoints = entry;
    return entry;
}

static void release_checkpoint_surface(RenderCheckpoint* entry) {
    if (entry->display_list) {
        dl_destroy(entry->display_list); mem_free(entry->display_list); entry->display_list = nullptr;
    } else if (entry->surface) image_surface_snapshot_release(entry->surface);
    entry->surface = nullptr;
    if (entry->pixels) mem_free(entry->pixels);
    entry->pixels = nullptr;
    entry->bytes = 0;
}

void event_sim_render_free(EventSimContext* context) {
    if (!context->render_state) return;
    auto* entry = context->render_state->checkpoints;
    while (entry) {
        auto* next = entry->next;
        // leases are released while document resources and the surface table still exist.
        release_checkpoint_surface(entry);
        mem_free(entry->name); mem_free(entry); entry = next;
    }
    mem_free(context->render_state); context->render_state = nullptr;
}

static double number(ItemReader item, double fallback = 0) {
    if (item.isInt()) return item.asInt();
    if (item.isFloat()) return item.asFloat();
    return fallback;
}
static const char* text(MapReader& args, const char* key, const char* fallback = "") {
    const char* value = args.get(key).cstring(); return value ? value : fallback;
}
static DomElement* target(UiContext* ui, MapReader& args) {
    const char* selector = args.get("target").cstring();
    if (!selector && args.get("target").isMap()) selector = args.get("target").asMap().get("selector").cstring();
    DomNode* node = ui && ui->document ? event_sim_find_element(ui->document, selector ? selector : "scene3d") : nullptr;
    return node && node->is_element() ? node->as_element() : nullptr;
}
static Runtime* bind_runtime(UiContext* ui) {
    if (ui && ui->document && !dom_document_script_runtime(ui->document))
        radiant_document_ensure_evaluator(ui->document);
    Runtime* runtime = ui && ui->document ? dom_document_script_runtime(ui->document) : nullptr;
    EvalContext* owner = runtime_get_eval_context(runtime);
    if (!runtime || !radiant_eval_context_switch(owner)) return nullptr;
    // a script-less page's evaluator has no heap until its first script turn.
    bool reusing = false;
    if ((!runtime_heap(runtime) && !js_prepare_eval_context(runtime, true, &owner, &reusing)) ||
        !runtime_context_bind_retained(runtime, owner) || !js_runtime_state_ensure_input(owner) ||
        !radiant_bind_document_script_host(ui, ui->document)) return nullptr;
    return runtime;
}

static AnimationInstance* css_instance(UiContext* ui, DomElement* element, bool resolve = false) {
    if (!element || !ui->document || !state_store_create(ui->document)) return nullptr;
    if (resolve) {
        LayoutContext layout = {};
        layout.doc = lam::up(ui->document); layout.pool = lam::up(ui->document->view_tree->prop_pool);
        layout.ui_context = lam::up(ui); layout.view = lam::up((View*)element); layout.elmt = lam::up(element);
        css_animation_resolve(element, &layout);
    }
    auto* scheduler = ui->document->state->animation_scheduler;
    for (auto* instance = scheduler ? scheduler->first : nullptr; instance; instance = instance->next)
        if (instance->target == element) return instance;
    return nullptr;
}
static bool evaluate(UiContext* ui, const char* source, bool assertion) {
    Runtime* runtime = bind_runtime(ui);
    if (!runtime || !source) return false;
    // eval borrows the host; publishing it as an owned document would recurse during teardown.
    void* document = runtime->dom_doc;
    void* host = runtime->dom_ui_context;
    runtime->dom_doc = ui->document;
    runtime->dom_ui_context = ui;
    // the evaluated code and its return value remain precise roots across eval safepoints (D5.3.3).
    RootFrame roots(2);
    Rooted<Item> code(roots, make_string_item(source)), result(roots, ItemNull);
    result.set(js_builtin_eval(code.get()));
    runtime->dom_doc = document;
    runtime->dom_ui_context = host;
    if (item_is_error(result.get())) return false;
    return !assertion || (get_type_id(result.get()) == LMD_TYPE_BOOL && result.get().bool_val);
}

static void graphics_metrics(RenderMetrics* metrics, const NativeGlStats& stats) {
#define METRIC(field) metrics->put("graphics." #field, stats.field)
    METRIC(generation); METRIC(allocated_bytes); METRIC(draws); METRIC(frames);
    METRIC(gpu_bytes); METRIC(cpu_bytes); METRIC(resources);
    METRIC(shader_normalize_calls); METRIC(shader_normalize_us);
#undef METRIC
    metrics->put("graphics.driver_present", stats.driver[0] != 0);
}

static void surface_metrics(RenderMetrics* metrics, ImageSurface* image) {
    metrics->put("exists", image != nullptr);
    if (!image) return;
    metrics->put("surface_id", (uintptr_t)image);
    metrics->put("generation", image->generation);
    metrics->put("width", image->width); metrics->put("height", image->height);
    metrics->put("premultiplied", image->alpha_mode == IMAGE_ALPHA_PREMULTIPLIED);
}

static ImageSurface* read_surface(EventSimContext* context, UiContext* ui, MapReader& args) {
    const char* saved = args.get("surface").cstring();
    if (saved) { auto* entry = checkpoint(context, saved); return entry ? entry->surface : nullptr; }
    const char* source = text(args, "source", "page");
    if (!strcmp(source, "page")) {
        if (!args.has("render") || args.get("render").asBool()) event_sim_render_surface(ui);
        return ui->surface;
    }
    auto* element = target(ui, args);
    if (!element) return nullptr;
    if (!strcmp(source, "canvas")) return render_canvas_surface((ViewBlock*)element);
    if (!strcmp(source, "scene3d")) return scene3d_snapshot(element, ui,
        number(args.get("width"), element->width), number(args.get("height"), element->height),
        number(args.get("scale"), 1));
    return nullptr;
}

static bool read_metrics(EventSimContext* context, UiContext* ui, MapReader& args, RenderMetrics* metrics) {
    const char* source = text(args, "source", "scene3d");
    auto* element = target(ui, args);
    if (!strcmp(source, "scene3d")) {
        Scene3dStats stats = {};
        bool exists = element && scene3d_stats(element, &stats);
        metrics->put("exists", exists);
        if (!exists) return true;
        graphics_metrics(metrics, stats.graphics);
#define METRIC(field) metrics->put(#field, stats.field)
        METRIC(projection_generation); METRIC(snapshot_generation);
        METRIC(projection_bytes); METRIC(projection_reserved_bytes); METRIC(snapshot_bytes);
        METRIC(meshes); METRIC(geometries); METRIC(textures);
        METRIC(raster_width); METRIC(raster_height); METRIC(camera_aspect);
#undef METRIC
        metrics->put("animation_id", (uintptr_t)scene3d_animations(element));
    } else if (!strcmp(source, "webgl")) {
        NativeGlStats stats = {};
        bool exists = element && radiant_webgl_stats(element, &stats);
        metrics->put("exists", exists);
        if (exists) graphics_metrics(metrics, stats);
    } else if (!strcmp(source, "animation")) {
        auto* animation = element ? scene3d_animations(element) : nullptr;
        auto* action = animation ? scene3d_animation_action(animation, text(args, "clip")) : nullptr;
        metrics->put("exists", action != nullptr);
        if (action) {
            metrics->put("animation_id", (uintptr_t)animation); metrics->put("action_id", (uintptr_t)action);
            metrics->put("time", action->time); metrics->put("active", animation_mixer_active(action->mixer));
            metrics->put("paused", action->paused); metrics->put("enabled", action->enabled);
        }
    } else if (!strcmp(source, "svg_animation")) {
        if (!element) return false;
        metrics->put("time", svg_animation_current_time(element)); metrics->put("paused", svg_animation_paused(element));
    } else if (!strcmp(source, "scheduler")) {
        auto* scheduler = ui->document && ui->document->state ? ui->document->state->animation_scheduler : nullptr;
        if (!scheduler) return false;
        metrics->put("count", scheduler->count); metrics->put("time", scheduler->current_time);
    } else if (!strcmp(source, "view")) {
        if (!element) return false;
        metrics->put("width", element->width); metrics->put("height", element->height);
        Element* backing = dom_element_backing(element);
        if (backing) {
            auto attribute = ElementReader(backing).get_attr(text(args, "attribute", "width"));
            metrics->put("attribute_numeric", attribute.isNumber());
            if (attribute.isNumber()) metrics->put("attribute_value", number(attribute));
            SvgIntrinsicSize size = calculate_svg_intrinsic_size(backing);
            metrics->put("intrinsic_width_present", size.has_intrinsic_width);
            metrics->put("intrinsic_width", size.width);
        }
        if (element->boundary() && element->boundary()->background) {
            metrics->put("background_position_x", element->boundary()->background->bg_position_x);
            metrics->put("background_position_x_is_percent", element->boundary()->background->bg_position_x_is_percent);
            metrics->put("background_repeat_x", element->boundary()->background->bg_repeat_x == CSS_VALUE_REPEAT);
            metrics->put("background_repeat_y", element->boundary()->background->bg_repeat_y == CSS_VALUE_REPEAT);
        }
    } else if (!strcmp(source, "document")) {
        metrics->put("shared_runtime", ui->document->js.runtime == ui->document->lambda_runtime);
    } else if (!strcmp(source, "css_animation")) {
        auto* instance = css_instance(ui, element);
        if (!instance) return false;
        metrics->put("layout_changed", instance->layout_changed);
    } else if (!strcmp(source, "measurement")) {
        auto* entry = checkpoint(context, text(args, "name"));
        if (!entry) return false;
        *metrics = entry->metrics;
    } else if (!strcmp(source, "surface")) {
        auto* entry = checkpoint(context, args.get("surface").cstring());
        if (!entry) return false;
        auto* image = image_surface_lookup(entry->handle);
        surface_metrics(metrics, image);
        metrics->put("immutable", image && entry->pixels && entry->bytes == (size_t)image->pitch * image->height &&
            !memcmp(entry->pixels, image->pixels, entry->bytes));
    } else return false;
    return true;
}

static bool compare_metrics(EventSimContext* context, MapReader& args, const RenderMetrics& metrics) {
    ItemReader expectations = args.get("fields");
    if (!expectations.isMap() || expectations.asMap().isEmpty()) return false;
    auto fields = expectations.asMap(); auto entries = fields.entries();
    const char* field; ItemReader expected; bool passed = true;
    while (entries.next(&field, &expected)) {
        double actual;
        if (!metrics.get(field, &actual) || !isfinite(actual)) {
            log_error("event_sim_render: missing metric '%s'", field); passed = false; continue;
        }
        if (expected.isInt() || expected.isFloat()) {
            if (!isfinite(number(expected)) || fabs(actual - number(expected)) > 1e-6) {
                log_error("event_sim_render: %s expected %.9g, got %.9g", field, number(expected), actual); passed = false;
            }
            continue;
        }
        if (!expected.isMap()) { passed = false; continue; }
        auto bounds = expected.asMap(); double reference = 0;
        double tolerance = number(bounds.get("tolerance"), 1e-6);
        if (!isfinite(tolerance) || tolerance < 0) { passed = false; continue; }
        if (bounds.has("baseline")) {
            auto* baseline = checkpoint(context, bounds.get("baseline").cstring());
            if (!baseline || !baseline->metrics.get(field, &reference)) { passed = false; continue; }
            actual -= reference;
        }
        auto comparisons = bounds.entries(); const char* op; ItemReader limit; unsigned checks = 0;
        while (comparisons.next(&op, &limit)) {
            if (!strcmp(op, "baseline")) continue;
            double value = number(limit, NAN); bool match = false;
            if (!strcmp(op, "eq")) match = isfinite(value) && fabs(actual - value) <= tolerance;
            else if (!strcmp(op, "ne")) match = isfinite(value) && actual != value;
            else if (!strcmp(op, "min")) match = actual >= value;
            else if (!strcmp(op, "max")) match = actual <= value;
            else if (!strcmp(op, "tolerance")) continue;
            checks++;
            if (!match) { log_error("event_sim_render: %s %s %.9g failed: actual/delta %.9g", field, op, value, actual); passed = false; }
        }
        if (!checks) passed = false;
    }
    return passed;
}

static bool save_surface(EventSimContext* context, UiContext* ui, MapReader& args) {
    auto* image = read_surface(context, ui, args);
    auto* entry = checkpoint(context, text(args, "name"), true);
    if (!image || !image->pixels || !entry) return false;
    // retain before replacing a same-name checkpoint, which may hold the last existing lease.
    ImageSurface* retained = image_surface_snapshot_retain(image);
    if (!retained) {
        // Mutable page surfaces cannot grant snapshot leases; publish an owned pixel copy instead.
        retained = image_surface_create(image->width, image->height);
        if (!retained) return false;
        retained->snapshot_refs = 1;
        retained->alpha_mode = image->alpha_mode;
        retained->generation = image->generation;
        for (size_t y = 0; y < (size_t)image->height; y++)
            memcpy((uint8_t*)retained->pixels + y * retained->pitch,
                (uint8_t*)image->pixels + y * image->pitch, (size_t)image->width * 4);
    }
    release_checkpoint_surface(entry);
    image = retained;
    entry->surface = image; entry->handle = image->self;
    if (!strcmp(text(args, "lease"), "display_list")) {
        entry->display_list = (DisplayList*)mem_calloc(1, sizeof(DisplayList), MEM_CAT_RENDER);
        if (!entry->display_list) return false;
        dl_init(entry->display_list, nullptr);
        dl_blit_surface_scaled(entry->display_list, image, 0, 0, image->width, image->height,
            SCALE_MODE_LINEAR, nullptr, nullptr, 0, 255, image->generation);
        image_surface_snapshot_release(retained);
    }
    entry->bytes = (size_t)image->pitch * image->height;
    entry->pixels = (uint8_t*)mem_alloc(entry->bytes, MEM_CAT_RENDER);
    if (!entry->pixels) return false;
    memcpy(entry->pixels, image->pixels, entry->bytes);
    entry->metrics = {}; surface_metrics(&entry->metrics, image);
    return true;
}

static bool assert_surface(EventSimContext* context, UiContext* ui, MapReader& args) {
    auto* image = read_surface(context, ui, args);
    if (!image || !image->pixels) return false;
    bool passed = true; unsigned checks = 0;
    auto pixels = args.get("pixels");
    if (pixels.isArray()) for (int64_t i = 0; i < pixels.asArray().length(); i++) {
        auto probe = pixels.asArray().get(i).asArray();
        if (probe.length() < 5 || probe.length() > 7) { passed = false; continue; }
        double x = number(probe.get(0), -1), y = number(probe.get(1), -1);
        if (x < 0 || y < 0 || x >= image->width || y >= image->height || x != floor(x) || y != floor(y)) { passed = false; continue; }
        const auto* pixel = (uint8_t*)image->pixels + (size_t)y * image->pitch + (size_t)x * 4;
        double tolerance = number(probe.get(6), 3);
        SimEvent bounds = {};
        int* minima[] = {&bounds.pixel_min_r, &bounds.pixel_min_g, &bounds.pixel_min_b, &bounds.pixel_min_a};
        int* maxima[] = {&bounds.pixel_max_r, &bounds.pixel_max_g, &bounds.pixel_max_b, &bounds.pixel_max_a};
        for (unsigned c = 0; c < 4; c++) {
            double expected = number(probe.get(c + 2), c == 3 ? 255 : 0);
            if (!isfinite(expected) || !isfinite(tolerance) || expected < 0 || expected > 255 || tolerance < 0) return false;
            *minima[c] = expected > tolerance ? ceil(expected - tolerance) : 0;
            *maxima[c] = expected + tolerance < 255 ? floor(expected + tolerance) : 255;
        }
        uint32_t rgba; memcpy(&rgba, pixel, sizeof(rgba));
        if (!event_sim_pixel_matches_bounds(rgba, &bounds, true)) {
            log_error("event_sim_render: pixel (%.0f,%.0f) failed", x, y); passed = false;
        }
        checks++;
    }
    auto region = args.get("region").asArray();
    double x = number(region.get(0)), y = number(region.get(1));
    double width = number(region.get(2), image->width - x), height = number(region.get(3), image->height - y);
    if (!isfinite(x) || !isfinite(y) || !isfinite(width) || !isfinite(height) ||
        x != floor(x) || y != floor(y) || width != floor(width) || height != floor(height) ||
        x < 0 || y < 0 || width < 0 || height < 0 || x + width > image->width || y + height > image->height) return false;
    const char* reference = args.get("reference").cstring();
    auto* previous = reference ? checkpoint(context, reference) : nullptr;
    if (reference && (!previous || !previous->pixels || previous->bytes != (size_t)image->pitch * image->height)) return false;
    if (reference || args.has("count")) {
        if (!args.get("count").isMap() || args.get("count").asMap().isEmpty()) return false;
        size_t count = 0;
        auto predicate = args.get("rgba").asArray();
        auto different = args.get("different_from_pixel").asArray();
        const uint8_t* background = nullptr;
        if (args.has("different_from_pixel")) {
            double bx = number(different.get(0), -1), by = number(different.get(1), -1);
            if (different.length() != 2 || bx < 0 || by < 0 || bx >= image->width || by >= image->height ||
                bx != floor(bx) || by != floor(by)) return false;
            background = (uint8_t*)image->pixels + (size_t)by * image->pitch + (size_t)bx * 4;
        }
        for (size_t row = (size_t)y; row < y + height; row++) for (size_t column = (size_t)x; column < x + width; column++) {
            size_t offset = row * image->pitch + column * 4;
            const auto* p = (uint8_t*)image->pixels + offset;
            bool match = reference ? memcmp(p, previous->pixels + offset, args.has("alpha") && args.get("alpha").asBool() ? 4 : 3) != 0 : true;
            if (background) match &= memcmp(p, background, 3) != 0;
            if (args.get("red_above_blue").asBool()) match &= p[0] > p[2];
            for (unsigned c = 0; c < predicate.length() && c < 4; c++) {
                auto bounds = predicate.get(c).asArray();
                match &= p[c] >= number(bounds.get(0)) && p[c] <= number(bounds.get(1), 255);
            }
            if (match) count++;
        }
        auto bounds = args.get("count").asMap();
        auto limits = bounds.entries(); const char* key; ItemReader limit;
        while (limits.next(&key, &limit)) {
            if ((strcmp(key, "min") && strcmp(key, "max") && strcmp(key, "eq")) ||
                !isfinite(number(limit, NAN)) || number(limit) < 0) return false;
        }
        passed &= count >= number(bounds.get("min")) && count <= number(bounds.get("max"), INFINITY);
        if (bounds.has("eq")) passed &= count == number(bounds.get("eq"));
        if (!passed) log_error("event_sim_render: region count %zu did not satisfy expected bounds", count);
        checks++;
    }
    if (args.has("premultiplied")) {
        passed &= (image->alpha_mode == IMAGE_ALPHA_PREMULTIPLIED) == args.get("premultiplied").asBool(); checks++;
    }
    if (args.get("premultiplied_pixels").asBool()) {
        for (size_t row = 0; row < (size_t)image->height; row++) for (size_t column = 0; column < (size_t)image->width; column++) {
            const auto* p = (uint8_t*)image->pixels + row * image->pitch + column * 4;
            passed &= p[0] <= p[3] && p[1] <= p[3] && p[2] <= p[3];
        }
        checks++;
    }
    return passed && checks;
}

static bool scene_command(EventSimContext* context, UiContext* ui, MapReader& args) {
    auto* element = target(ui, args); if (!element) return false;
    const char* op = text(args, "op", "snapshot");
    if (!strcmp(op, "snapshot")) {
        ImageSurface* image = scene3d_snapshot(element, ui, number(args.get("width"), 128),
            number(args.get("height"), 128), number(args.get("scale"), 1));
        bool expected = !args.has("success") || args.get("success").asBool();
        const char* diagnostic = args.get("diagnostic").cstring();
        return (image != nullptr) == expected && (!diagnostic || strstr(scene3d_diagnostic(element), diagnostic));
    }
    if (!strcmp(op, "lose_context")) { scene3d_context_lost(element); return true; }
    if (!strcmp(op, "release")) { scene3d_release_subtree(element); return true; }
    if (!strcmp(op, "collect")) { scene3d_collect(ui->document); return true; }
    auto* animation = scene3d_animations(element); if (!animation) return false;
    if (!strcmp(op, "seek")) return scene3d_animation_seek(animation, number(args.get("seconds"), NAN));
    if (!strcmp(op, "automatic")) return scene3d_animation_automatic(animation, args.get("enabled").asBool());
    if (!strcmp(op, "update")) {
        double seconds = number(args.get("seconds"), NAN);
        if (!isfinite(seconds) || seconds < 0) return false;
        scene3d_animation_update(animation, seconds); return true;
    }
    auto* action = scene3d_animation_action(animation, text(args, "clip")); if (!action) return false;
    if (!strcmp(op, "play")) return animation_action_play(action);
    if (!strcmp(op, "stop")) return animation_action_stop(action);
    if (!strcmp(op, "configure")) {
        if (args.has("once")) action->loop = args.get("once").asBool() ? ANIMATION_LOOP_ONCE : ANIMATION_LOOP_REPEAT;
        if (args.has("clamp")) action->clamp = args.get("clamp").asBool();
        return true;
    }
    return false;
}

static StrBuf* artifact_path(MapReader& args) {
    StrBuf* path = strbuf_new();
    const char* directory_env = args.get("directory_env").cstring();
    const char* directory = directory_env ? getenv(directory_env) : nullptr;
    if (directory_env && (!directory || !directory[0])) { strbuf_free(path); return nullptr; }
    if (directory) strbuf_append_format(path, "%s/", directory);
    strbuf_append_str(path, text(args, "file"));
    if (!event_sim_path_is_temp_scratch(path->str) || !event_sim_create_parent_dirs(path->str)) {
        strbuf_free(path); return nullptr;
    }
    return path;
}

static bool capture_surface(EventSimContext* context, UiContext* ui, MapReader& args) {
    auto* image = read_surface(context, ui, args);
    StrBuf* path = artifact_path(args);
    bool passed = image && image->pixels && path && image->pitch == image->width * 4;
    if (passed) passed = image_save_png(path->str, (const uint8_t*)image->pixels, image->width, image->height, 4) == 1;
    strbuf_free(path);
    return passed;
}

#ifdef NDEBUG
static void vector_cache_metrics(double* entries, double* bytes) {
    MemSnapshot* snapshot = mem_snapshot_capture(nullptr);
    if (snapshot) for (uint32_t i = 0; i < snapshot->count; i++) {
        if (!strcmp(snapshot->samples[i].label, "rdt.vector.caches")) {
            *entries = snapshot->samples[i].alloc_count;
            *bytes = snapshot->samples[i].bytes_in_use;
        }
    }
    mem_snapshot_free(snapshot);
}

static int compare_samples(const void* left, const void* right) {
    double a = *(const double*)left, b = *(const double*)right;
    return (a > b) - (a < b);
}
#endif

static bool measure_frames(EventSimContext* context, UiContext* ui, MapReader& args) {
#ifndef NDEBUG
    log_error("event_sim_render: frame measurements require a release build");
    return false;
#else
    double count = number(args.get("frames"), NAN);
    if (!isfinite(count) || count < 1 || count > 10000 || count != floor(count) ||
        !evaluate(ui, args.get("setup").cstring(), false)) return false;
    auto* element = target(ui, args);
    NativeGlStats before = {}, after = {};
    if (!element || !radiant_webgl_stats(element, &before)) return false;
    auto* entry = checkpoint(context, text(args, "name"), true);
    StrBuf* path = artifact_path(args);
    if (!entry || !path) { strbuf_free(path); return false; }
    size_t frames = (size_t)count;
    auto* durations = (double*)mem_calloc(frames * 2, sizeof(double), MEM_CAT_TEMP);
    if (!durations) { strbuf_free(path); return false; }
    double* sampling = durations + frames;
    double entries_before = 0, bytes_before = 0, entries_after = 0, bytes_after = 0;
    vector_cache_metrics(&entries_before, &bytes_before);
    RootFrame roots(4);
    Rooted<Item> global(roots, dom_realm_global());
    Rooted<Item> callback(roots, dom_realm_get_name(global.get(), text(args, "callback")));
    Rooted<Item> sample(roots, ItemNull), result(roots, ItemNull);
    // performance.now() must observe wall time inside the callback, not the fixture's frozen clock.
    bool virtual_clock = js_event_loop_virtual_clock_enabled();
    double virtual_ms = js_event_loop_virtual_clock_now_ms();
    bool document_clock = ui->document->js.virtual_clock_enabled;
    ui->document->js.virtual_clock_enabled = false;
    js_event_loop_set_virtual_clock(false, 0);
    bool passed = true;
    for (size_t i = 0; i < frames && passed; i++) {
        uint64_t start = time_now_ns();
        result.set(dom_realm_call(callback.get(), ItemNull, nullptr, 0));
        sample.set(dom_realm_get_name(global.get(), text(args, "sample")));
        passed = !item_is_error(result.get()) && item_try_to_double(sample.get(), &sampling[i]);
        if (passed) event_sim_render_surface(ui);
        passed &= !ui->render_failed;
        durations[i] = time_elapsed_ms_f(start, time_now_ns());
    }
    ui->document->js.virtual_clock_enabled = document_clock;
    js_event_loop_set_virtual_clock(virtual_clock, virtual_ms);
    if (passed) {
        qsort(durations, frames, sizeof(double), compare_samples);
        qsort(sampling, frames, sizeof(double), compare_samples);
        vector_cache_metrics(&entries_after, &bytes_after);
        passed = radiant_webgl_stats(element, &after);
        auto& metrics = entry->metrics; metrics = {};
        metrics.put("frames", frames);
        metrics.put("frame_p50_ms", durations[frames / 2]);
        metrics.put("frame_p95_ms", durations[frames * 95 / 100]);
        metrics.put("sampling_p50_ms", sampling[frames / 2]);
        metrics.put("sampling_p95_ms", sampling[frames * 95 / 100]);
        metrics.put("vector_cache_entries_before", entries_before);
        metrics.put("vector_cache_entries_after", entries_after);
        metrics.put("vector_cache_entries_growth", entries_after - entries_before);
        metrics.put("vector_cache_metadata_bytes_before", bytes_before);
        metrics.put("vector_cache_metadata_bytes_after", bytes_after);
        graphics_metrics(&metrics, after);
        metrics.put("graphics.cpu_bytes_growth", (double)after.cpu_bytes - before.cpu_bytes);
        metrics.put("graphics.gpu_bytes_growth", (double)after.gpu_bytes - before.gpu_bytes);
        if (args.has("fields")) passed &= compare_metrics(context, args, metrics);
        StrBuf* json = strbuf_new(); strbuf_append_char(json, '{');
        for (unsigned i = 0; i < metrics.count; i++)
            strbuf_append_format(json, "%s\"%s\":%.9g", i ? "," : "", metrics.values[i].name, metrics.values[i].value);
        strbuf_append_str(json, "}\n");
        passed &= write_text_file_atomic(path->str, json->str) == 0;
        strbuf_free(json);
    }
    mem_free(durations); strbuf_free(path);
    return passed;
#endif
}

bool event_sim_render_execute(EventSimContext* context, UiContext* ui, SimEvent* event) {
    if (!ui || !ui->document || !event->render_command) return false;
    // the per-command Input is released at the end of the operation, including failed assertions.
    Input* input = InputManager::create_input(nullptr);
    if (!input) return false;
    parse_json(input, event->render_command->json);
    auto args = ItemReader(input->root.to_const()).asMap();
    const char* name = event->render_command->name;
    bool passed = false;
    const char* enabled_env = args.get("enabled_env").cstring();
    if (enabled_env && !getenv(enabled_env)) passed = true;
    else if (!strcmp(name, "script") || !strcmp(name, "assert_script")) {
        passed = evaluate(ui, text(args, "js"), !strcmp(name, "assert_script"));
    } else if (!strcmp(name, "collect_gc")) {
        Runtime* runtime = bind_runtime(ui);
        double count = number(args.get("count"), 1);
        passed = runtime && count >= 1 && count <= 100 && count == floor(count);
        if (passed) for (unsigned i = 0; i < count; i++) gc_collect(runtime_heap(runtime)->gc, nullptr, 0);
    } else if (!strcmp(name, "scene3d")) passed = scene_command(context, ui, args);
    else if (!strcmp(name, "prepare_canvas")) { radiant_canvas_prepare_document(ui->document); passed = true; }
    else if (!strcmp(name, "capture_surface")) passed = capture_surface(context, ui, args);
    else if (!strcmp(name, "measure_frames")) passed = measure_frames(context, ui, args);
    else if (!strcmp(name, "snapshot_surface")) passed = save_surface(context, ui, args);
    else if (!strcmp(name, "release_surface")) {
        auto* entry = checkpoint(context, text(args, "name"));
        passed = entry && entry->surface;
        if (passed) release_checkpoint_surface(entry);
    } else if (!strcmp(name, "assert_surface")) passed = assert_surface(context, ui, args);
    else if (!strcmp(name, "snapshot_render_stats") || !strcmp(name, "assert_render_stats")) {
        RenderMetrics metrics = {};
        // a script and its resource observation can share a turn, before the host paints again.
        passed = (!args.has("js") || evaluate(ui, args.get("js").cstring(), false)) &&
            read_metrics(context, ui, args, &metrics);
        if (passed && !strcmp(name, "snapshot_render_stats")) {
            auto* entry = checkpoint(context, text(args, "name"), true);
            passed = entry != nullptr; if (passed) entry->metrics = metrics;
        } else if (passed) passed = compare_metrics(context, args, metrics);
    } else if (!strcmp(name, "animation_tick")) {
        auto* scheduler = ui->document->state ? ui->document->state->animation_scheduler : nullptr;
        double seconds = number(args.get("seconds"), NAN);
        passed = scheduler && isfinite(seconds) && seconds >= 0;
        if (passed) {
            bool active = animation_scheduler_tick(scheduler, scheduler->current_time + seconds, nullptr);
            if (args.has("active")) passed = active == args.get("active").asBool();
        }
    } else if (!strcmp(name, "animation_frame")) {
        double time = number(args.get("ms"), NAN);
        passed = bind_runtime(ui) && isfinite(time) && time >= 0;
        if (passed) {
            int callbacks = js_animation_frame_flush(time);
            if (args.has("callbacks")) passed = callbacks == number(args.get("callbacks"));
        }
    } else if (!strcmp(name, "css_animation")) {
        auto* instance = css_instance(ui, target(ui, args), true);
        double seconds = number(args.get("seconds"), NAN);
        passed = instance && isfinite(seconds) && seconds >= 0;
        if (passed) { instance->layout_changed = false; css_animation_tick(instance, seconds); }
    } else if (!strcmp(name, "pointer_event")) {
        auto* element = target(ui, args);
        double x = number(args.get("x"), NAN), y = number(args.get("y"), NAN);
        if (args.get("position_from").isMap()) {
            auto position = args.get("position_from").asMap();
            auto* source = target(ui, position);
            const char* sx = source ? source->get_attribute(text(position, "x")) : nullptr;
            const char* sy = source ? source->get_attribute(text(position, "y")) : nullptr;
            char *end_x = nullptr, *end_y = nullptr;
            x = sx ? strtod(sx, &end_x) : NAN; y = sy ? strtod(sy, &end_y) : NAN;
            if ((end_x && *end_x) || (end_y && *end_y)) x = NAN;
        }
        const char* kind = text(args, "event");
        passed = element && isfinite(x) && isfinite(y);
        if (passed && (!strcmp(kind, "mousedown") || !strcmp(kind, "mouseup"))) {
            RdtEvent input_event = {};
            input_event.type = !strcmp(kind, "mousedown") ? RDT_EVENT_MOUSE_DOWN : RDT_EVENT_MOUSE_UP;
            input_event.mouse_button.x = x; input_event.mouse_button.y = y;
            input_event.mouse_button.button = number(args.get("button")); input_event.mouse_button.clicks = 1;
            handle_event(ui, ui->document, &input_event);
        } else if (passed && (!strcmp(kind, "pointerdown") || !strcmp(kind, "pointerup") || !strcmp(kind, "pointercancel"))) {
            // this API returns preventDefault(), not dispatch success; cancellation is valid input.
            bool prevented = radiant_dispatch_event_sim_pointer(ui, element, kind, x, y,
                number(args.get("button")), number(args.get("buttons")), 0, "mouse");
            if (args.has("prevented")) passed = prevented == args.get("prevented").asBool();
        } else passed = false;
    }
    input_release_document_resources(input);
    if (!passed) log_error("event_sim_render: %s FAIL (%s)", name, event->render_command->json);
    else log_info("event_sim_render: %s PASS", name);
    return passed;
}
