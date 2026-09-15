#include "render.hpp"
#include "radiant.hpp"
#include "glyph_sampling.hpp"

#include "../lib/tagged.hpp"
#include "../lib/font/font.h"
#include "../lambda/input/css/dom_element.hpp"
#include "../lambda/module/radiant/radiant_dom_bridge.hpp"
#include "../lib/log.h"
#include "../lib/mem.h"
#include "../lib/str.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// Keep this bounded while ImageSurface owns an implementation-specific pixel
// allocation.  A rejected resize leaves the prior bitmap intact.
#define RADIANT_CANVAS_MAX_DIMENSION 4096
#define RADIANT_CANVAS_MAX_PIXELS 4194304u

enum CanvasTextAlign {
    CANVAS_TEXT_ALIGN_START = 0,
    CANVAS_TEXT_ALIGN_END,
    CANVAS_TEXT_ALIGN_LEFT,
    CANVAS_TEXT_ALIGN_RIGHT,
    CANVAS_TEXT_ALIGN_CENTER,
};

struct CanvasState {
    RdtMatrix transform;
    Color fill_color;
    Color stroke_color;
    float line_width;
    float global_alpha;
    RdtStrokeCap line_cap;
    RdtStrokeJoin line_join;
    CanvasTextAlign text_align;
    char* font;
};

struct CanvasSavedState {
    CanvasState state;
    int clip_depth;
    CanvasSavedState* next;
};

struct CanvasClip {
    RdtPath* path;
    RdtMatrix transform;
    CanvasClip* next;
};

struct CanvasEntry {
    DomElement* element;
    ImageSurface* surface;
    CanvasState state;
    CanvasSavedState* saved_states;
    CanvasClip* clips;
    int clip_depth;
    RdtPath* path;
    bool path_has_point;
    float path_current_x;
    float path_current_y;
    float path_subpath_x;
    float path_subpath_y;
    bool path_has_subpath;
    CanvasEntry* next;
};

struct CanvasRegistry {
    CanvasEntry* entries;
};

static Color canvas_color(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    Color color = {};
    color.r = r;
    color.g = g;
    color.b = b;
    color.a = a;
    return color;
}

static CanvasState canvas_initial_state(void) {
    CanvasState state = {};
    state.transform = rdt_matrix_identity();
    state.fill_color = canvas_color(0, 0, 0, 255);
    state.stroke_color = canvas_color(0, 0, 0, 255);
    state.line_width = 1.0f;
    state.global_alpha = 1.0f;
    state.line_cap = RDT_CAP_BUTT;
    state.line_join = RDT_JOIN_MITER;
    state.text_align = CANVAS_TEXT_ALIGN_START;
    return state;
}

static void canvas_state_destroy(CanvasState* state) {
    if (!state) return;
    if (state->font) mem_free(state->font);
    state->font = nullptr;
}

static bool canvas_state_set_font(CanvasState* state, const char* font, int font_len) {
    if (!state || !font || font_len <= 0 ||
        font_len >= RADIANT_CANVAS_FONT_TEXT_MAX) return false;
    char* copy = mem_dup_n(font, (size_t)font_len, MEM_CAT_LAYOUT);
    if (!copy) return false;
    if (state->font) mem_free(state->font);
    state->font = copy;
    return true;
}

static bool canvas_state_copy(CanvasState* destination, const CanvasState* source) {
    if (!destination || !source) return false;
    *destination = *source;
    destination->font = nullptr;
    if (!source->font) return true;
    return canvas_state_set_font(destination, source->font, (int)strlen(source->font));
}

static void canvas_free_saved_states(CanvasEntry* entry) {
    if (!entry) return;
    CanvasSavedState* saved = entry->saved_states;
    while (saved) {
        CanvasSavedState* next = saved->next;
        canvas_state_destroy(&saved->state);
        mem_free(saved);
        saved = next;
    }
    entry->saved_states = nullptr;
}

static void canvas_free_clips(CanvasEntry* entry) {
    if (!entry) return;
    CanvasClip* clip = entry->clips;
    while (clip) {
        CanvasClip* next = clip->next;
        if (clip->path) rdt_path_free(clip->path);
        mem_free(clip);
        clip = next;
    }
    entry->clips = nullptr;
    entry->clip_depth = 0;
}

static void canvas_pop_clips_to_depth(CanvasEntry* entry, int depth) {
    if (!entry) return;
    while (entry->clips && entry->clip_depth > depth) {
        CanvasClip* clip = entry->clips;
        entry->clips = clip->next;
        if (clip->path) rdt_path_free(clip->path);
        mem_free(clip);
        entry->clip_depth--;
    }
}

static void canvas_reset_state(CanvasEntry* entry) {
    if (!entry) return;
    canvas_free_saved_states(entry);
    canvas_free_clips(entry);
    if (entry->path) rdt_path_free(entry->path);
    entry->path = rdt_path_new();
    entry->path_has_point = false;
    entry->path_has_subpath = false;
    canvas_state_destroy(&entry->state);
    entry->state = canvas_initial_state();
}

static void canvas_entry_destroy(CanvasEntry* entry) {
    if (!entry) return;
    canvas_free_saved_states(entry);
    canvas_free_clips(entry);
    if (entry->path) rdt_path_free(entry->path);
    canvas_state_destroy(&entry->state);
    if (entry->surface) image_surface_destroy(entry->surface);
    mem_free(entry);
}

static void canvas_registry_destroy(void* data) {
    CanvasRegistry* registry = (CanvasRegistry*)data;
    if (!registry) return;
    CanvasEntry* entry = registry->entries;
    while (entry) {
        CanvasEntry* next = entry->next;
        canvas_entry_destroy(entry);
        entry = next;
    }
    mem_free(registry);
}

static CanvasRegistry* canvas_registry_for_document(DomDocument* document,
                                                     bool create) {
    if (!document) return nullptr;
    CanvasRegistry* registry = (CanvasRegistry*)document->services.canvas_registry;
    if (registry || !create) return registry;
    registry = (CanvasRegistry*)mem_calloc(1, sizeof(CanvasRegistry), MEM_CAT_LAYOUT);
    if (!registry) return nullptr;
    if (!dom_document_add_resource(document, registry, canvas_registry_destroy)) {
        mem_free(registry);
        return nullptr;
    }
    document->services.canvas_registry = registry;
    return registry;
}

static int canvas_attribute_dimension(DomElement* element, const char* name,
                                      int fallback) {
    if (!element || !name) return fallback;
    const char* text = element->get_attribute(name);
    if (!text || !*text) return fallback;
    char* end = nullptr;
    long value = strtol(text, &end, 10);
    if (end == text || value < 0 || value > RADIANT_CANVAS_MAX_DIMENSION) {
        return fallback;
    }
    return (int)value; // INT_CAST_OK: value is range-checked canvas pixel dimension.
}

static bool canvas_dimensions_supported(int width, int height) {
    if (width < 0 || height < 0 ||
        width > RADIANT_CANVAS_MAX_DIMENSION ||
        height > RADIANT_CANVAS_MAX_DIMENSION) {
        return false;
    }
    uint64_t pixels = (uint64_t)width * (uint64_t)height;
    return pixels <= RADIANT_CANVAS_MAX_PIXELS;
}

static bool canvas_replace_surface(CanvasEntry* entry, int width, int height) {
    if (!entry || !canvas_dimensions_supported(width, height)) return false;
    ImageSurface* replacement = nullptr;
    if (width > 0 && height > 0) {
        replacement = image_surface_create(width, height);
        if (!replacement) return false;
    }
    if (entry->surface) image_surface_destroy(entry->surface);
    entry->surface = replacement;
    canvas_reset_state(entry);
    return true;
}

static CanvasEntry* canvas_find_entry(CanvasRegistry* registry,
                                      DomElement* element) {
    if (!registry || !element) return nullptr;
    for (CanvasEntry* entry = registry->entries; entry; entry = entry->next) {
        if (entry->element == element) return entry;
    }
    return nullptr;
}

static CanvasEntry* canvas_entry_for_element(DomElement* element, bool create) {
    if (!element || element->tag() != MARKUP_NAME_CANVAS) return nullptr;
    CanvasRegistry* registry = canvas_registry_for_document(element->doc, create);
    CanvasEntry* entry = canvas_find_entry(registry, element);
    if (entry || !create || !registry) return entry;

    entry = (CanvasEntry*)mem_calloc(1, sizeof(CanvasEntry), MEM_CAT_LAYOUT);
    if (!entry) return nullptr;
    entry->element = element;
    int width = canvas_attribute_dimension(element, "width", 300);
    int height = canvas_attribute_dimension(element, "height", 150);
    if (!canvas_replace_surface(entry, width, height)) {
        canvas_entry_destroy(entry);
        return nullptr;
    }
    entry->next = registry->entries;
    registry->entries = entry;
    return entry;
}

static void canvas_note_pixels_changed(CanvasEntry* entry) {
    if (entry && entry->surface) image_surface_bump_generation(entry->surface);
}

static Color canvas_effective_color(Color color, float global_alpha) {
    color.a = (uint8_t)((float)color.a * global_alpha + 0.5f);
    return color;
}

static int canvas_push_clips(CanvasEntry* entry, RdtVector* vector,
                             int* saved_clip_depth) {
    if (!entry || !vector || !saved_clip_depth) return 0;
    // RdtVector's clip stack is thread-local, so each canvas draw isolates it.
    *saved_clip_depth = rdt_clip_save_depth();
    int pushed = 0;
    for (CanvasClip* clip = entry->clips; clip; clip = clip->next) {
        rdt_push_clip(vector, clip->path, &clip->transform);
        pushed++;
    }
    return pushed;
}

static void canvas_pop_clips(RdtVector* vector, int pushed, int saved_clip_depth) {
    for (int index = 0; index < pushed; index++) rdt_pop_clip(vector);
    rdt_clip_restore_depth(saved_clip_depth);
}

static bool canvas_draw_path(CanvasEntry* entry, bool stroke) {
    if (!entry || !entry->path || !entry->surface) return entry != nullptr;
    RdtVector vector = {};
    int stride = entry->surface->pitch / 4;
    rdt_vector_init(&vector, (uint32_t*)entry->surface->pixels,
                    entry->surface->width, entry->surface->height, stride);
    int saved_clip_depth = 0;
    int pushed_clips = canvas_push_clips(entry, &vector, &saved_clip_depth);
    if (stroke) {
        rdt_stroke_path(&vector, entry->path,
                        canvas_effective_color(entry->state.stroke_color,
                                               entry->state.global_alpha),
                        entry->state.line_width, entry->state.line_cap,
                        entry->state.line_join,
                        nullptr, 0, 0.0f, &entry->state.transform);
    } else {
        rdt_fill_path(&vector, entry->path,
                      canvas_effective_color(entry->state.fill_color,
                                             entry->state.global_alpha),
                      RDT_FILL_WINDING, &entry->state.transform);
    }
    canvas_pop_clips(&vector, pushed_clips, saved_clip_depth);
    rdt_vector_destroy(&vector);
    canvas_note_pixels_changed(entry);
    return true;
}

static bool canvas_draw_rect(CanvasEntry* entry, float x, float y,
                             float width, float height, bool stroke) {
    if (!entry) return false;
    if (!entry->surface) return true;
    // Canvas normalizes a negative rectangle extent to the opposite edge.
    if (width < 0.0f) {
        x += width;
        width = -width;
    }
    if (height < 0.0f) {
        y += height;
        height = -height;
    }
    RdtPath* path = rdt_path_new();
    if (!path) return false;
    rdt_path_add_rect(path, x, y, width, height, 0.0f, 0.0f);
    RdtVector vector = {};
    int stride = entry->surface->pitch / 4;
    rdt_vector_init(&vector, (uint32_t*)entry->surface->pixels,
                    entry->surface->width, entry->surface->height, stride);
    int saved_clip_depth = 0;
    int pushed_clips = canvas_push_clips(entry, &vector, &saved_clip_depth);
    if (stroke) {
        rdt_stroke_path(&vector, path,
                        canvas_effective_color(entry->state.stroke_color,
                                               entry->state.global_alpha),
                        entry->state.line_width, entry->state.line_cap,
                        entry->state.line_join,
                        nullptr, 0, 0.0f, &entry->state.transform);
    } else {
        rdt_fill_path(&vector, path,
                      canvas_effective_color(entry->state.fill_color,
                                             entry->state.global_alpha), RDT_FILL_WINDING,
                      &entry->state.transform);
    }
    canvas_pop_clips(&vector, pushed_clips, saved_clip_depth);
    rdt_vector_destroy(&vector);
    rdt_path_free(path);
    canvas_note_pixels_changed(entry);
    return true;
}

static bool canvas_ensure_path(CanvasEntry* entry) {
    if (!entry) return false;
    if (!entry->path) entry->path = rdt_path_new();
    return entry->path != nullptr;
}

extern "C" bool radiant_canvas_ensure(void* canvas_element) {
    return canvas_entry_for_element((DomElement*)canvas_element, true) != nullptr;
}

extern "C" bool radiant_canvas_set_dimension(void* canvas_element,
                                                bool is_width, uint32_t value) {
    DomElement* element = (DomElement*)canvas_element;
    CanvasEntry* entry = canvas_entry_for_element(element, true);
    if (!entry || value > (uint32_t)RADIANT_CANVAS_MAX_DIMENSION) return false;
    int width = entry->surface ? entry->surface->width
        : canvas_attribute_dimension(element, "width", 300);
    int height = entry->surface ? entry->surface->height
        : canvas_attribute_dimension(element, "height", 150);
    if (is_width) width = (int)value; // INT_CAST_OK: maximum canvas dimension validated above.
    else height = (int)value; // INT_CAST_OK: maximum canvas dimension validated above.
    return canvas_replace_surface(entry, width, height);
}

extern "C" bool radiant_canvas_reset_from_attributes(void* canvas_element) {
    DomElement* element = (DomElement*)canvas_element;
    CanvasEntry* entry = canvas_entry_for_element(element, true);
    if (!entry) return false;
    int width = canvas_attribute_dimension(element, "width", 300);
    int height = canvas_attribute_dimension(element, "height", 150);
    return canvas_replace_surface(entry, width, height);
}

extern "C" bool radiant_canvas_set_fill_color(void* canvas_element,
                                                 uint8_t r, uint8_t g,
                                                 uint8_t b, uint8_t a) {
    CanvasEntry* entry = canvas_entry_for_element((DomElement*)canvas_element, true);
    if (!entry) return false;
    entry->state.fill_color = canvas_color(r, g, b, a);
    return true;
}

extern "C" bool radiant_canvas_set_stroke_color(void* canvas_element,
                                                   uint8_t r, uint8_t g,
                                                   uint8_t b, uint8_t a) {
    CanvasEntry* entry = canvas_entry_for_element((DomElement*)canvas_element, true);
    if (!entry) return false;
    entry->state.stroke_color = canvas_color(r, g, b, a);
    return true;
}

extern "C" bool radiant_canvas_set_line_width(void* canvas_element, float width) {
    CanvasEntry* entry = canvas_entry_for_element((DomElement*)canvas_element, true);
    if (!entry || !isfinite(width) || width <= 0.0f) return false;
    entry->state.line_width = width;
    return true;
}

extern "C" bool radiant_canvas_set_global_alpha(void* canvas_element, float alpha) {
    CanvasEntry* entry = canvas_entry_for_element((DomElement*)canvas_element, true);
    if (!entry || !isfinite(alpha) || alpha < 0.0f || alpha > 1.0f) return false;
    entry->state.global_alpha = alpha;
    return true;
}

extern "C" bool radiant_canvas_set_line_cap(void* canvas_element, uint8_t cap) {
    CanvasEntry* entry = canvas_entry_for_element((DomElement*)canvas_element, true);
    if (!entry || cap > (uint8_t)RDT_CAP_SQUARE) return false;
    entry->state.line_cap = (RdtStrokeCap)cap;
    return true;
}

extern "C" bool radiant_canvas_set_line_join(void* canvas_element, uint8_t join) {
    CanvasEntry* entry = canvas_entry_for_element((DomElement*)canvas_element, true);
    if (!entry || join > (uint8_t)RDT_JOIN_BEVEL) return false;
    entry->state.line_join = (RdtStrokeJoin)join;
    return true;
}

extern "C" bool radiant_canvas_set_text_align(void* canvas_element, uint8_t align) {
    CanvasEntry* entry = canvas_entry_for_element((DomElement*)canvas_element, true);
    if (!entry || align > (uint8_t)CANVAS_TEXT_ALIGN_CENTER) return false;
    entry->state.text_align = (CanvasTextAlign)align;
    return true;
}

extern "C" bool radiant_canvas_set_font(void* canvas_element, const char* font,
                                            int font_len) {
    CanvasEntry* entry = canvas_entry_for_element((DomElement*)canvas_element, true);
    return entry && canvas_state_set_font(&entry->state, font, font_len);
}

extern "C" bool radiant_canvas_get_state(void* canvas_element,
                                            RadiantCanvasStateSnapshot* out) {
    CanvasEntry* entry = canvas_entry_for_element((DomElement*)canvas_element, true);
    if (!entry || !out) return false;
    memset(out, 0, sizeof(*out));
    out->fill_r = entry->state.fill_color.r;
    out->fill_g = entry->state.fill_color.g;
    out->fill_b = entry->state.fill_color.b;
    out->fill_a = entry->state.fill_color.a;
    out->stroke_r = entry->state.stroke_color.r;
    out->stroke_g = entry->state.stroke_color.g;
    out->stroke_b = entry->state.stroke_color.b;
    out->stroke_a = entry->state.stroke_color.a;
    out->line_width = entry->state.line_width;
    out->global_alpha = entry->state.global_alpha;
    out->line_cap = (uint8_t)entry->state.line_cap;
    out->line_join = (uint8_t)entry->state.line_join;
    out->text_align = (uint8_t)entry->state.text_align;
    const char* font = entry->state.font ? entry->state.font : "10px sans-serif";
    size_t font_len = strlen(font);
    if (font_len >= sizeof(out->font)) font_len = sizeof(out->font) - 1;
    memcpy(out->font, font, font_len);
    out->font[font_len] = '\0';
    return true;
}

extern "C" bool radiant_canvas_save(void* canvas_element) {
    CanvasEntry* entry = canvas_entry_for_element((DomElement*)canvas_element, true);
    if (!entry) return false;
    CanvasSavedState* saved = (CanvasSavedState*)mem_calloc(
        1, sizeof(CanvasSavedState), MEM_CAT_LAYOUT);
    if (!saved) return false;
    if (!canvas_state_copy(&saved->state, &entry->state)) {
        mem_free(saved);
        return false;
    }
    saved->clip_depth = entry->clip_depth;
    saved->next = entry->saved_states;
    entry->saved_states = saved;
    return true;
}

extern "C" bool radiant_canvas_restore(void* canvas_element) {
    CanvasEntry* entry = canvas_entry_for_element((DomElement*)canvas_element, true);
    if (!entry) return false;
    CanvasSavedState* saved = entry->saved_states;
    if (!saved) return true;
    canvas_state_destroy(&entry->state);
    entry->state = saved->state;
    saved->state.font = nullptr;
    canvas_pop_clips_to_depth(entry, saved->clip_depth);
    entry->saved_states = saved->next;
    mem_free(saved);
    return true;
}

extern "C" bool radiant_canvas_scale(void* canvas_element, float x, float y) {
    CanvasEntry* entry = canvas_entry_for_element((DomElement*)canvas_element, true);
    if (!entry || !isfinite(x) || !isfinite(y)) return false;
    RdtMatrix scale = {x, 0.0f, 0.0f, 0.0f, y, 0.0f, 0.0f, 0.0f, 1.0f};
    entry->state.transform = rdt_matrix_multiply(&entry->state.transform, &scale);
    return true;
}

extern "C" bool radiant_canvas_translate(void* canvas_element, float x, float y) {
    CanvasEntry* entry = canvas_entry_for_element((DomElement*)canvas_element, true);
    if (!entry || !isfinite(x) || !isfinite(y)) return false;
    RdtMatrix translation = rdt_matrix_translate(x, y);
    entry->state.transform = rdt_matrix_multiply(&entry->state.transform, &translation);
    return true;
}

extern "C" bool radiant_canvas_rotate(void* canvas_element, float radians) {
    CanvasEntry* entry = canvas_entry_for_element((DomElement*)canvas_element, true);
    if (!entry || !isfinite(radians)) return false;
    float cosine = cosf(radians);
    float sine = sinf(radians);
    RdtMatrix rotation = {cosine, -sine, 0.0f, sine, cosine, 0.0f,
                          0.0f, 0.0f, 1.0f};
    entry->state.transform = rdt_matrix_multiply(&entry->state.transform, &rotation);
    return true;
}

extern "C" bool radiant_canvas_begin_path(void* canvas_element) {
    CanvasEntry* entry = canvas_entry_for_element((DomElement*)canvas_element, true);
    if (!entry) return false;
    if (entry->path) rdt_path_free(entry->path);
    entry->path = rdt_path_new();
    entry->path_has_point = false;
    entry->path_has_subpath = false;
    return entry->path != nullptr;
}

extern "C" bool radiant_canvas_close_path(void* canvas_element) {
    CanvasEntry* entry = canvas_entry_for_element((DomElement*)canvas_element, true);
    if (!canvas_ensure_path(entry)) return false;
    rdt_path_close(entry->path);
    if (entry->path_has_subpath) {
        entry->path_current_x = entry->path_subpath_x;
        entry->path_current_y = entry->path_subpath_y;
        entry->path_has_point = true;
    }
    return true;
}

extern "C" bool radiant_canvas_move_to(void* canvas_element, float x, float y) {
    CanvasEntry* entry = canvas_entry_for_element((DomElement*)canvas_element, true);
    if (!canvas_ensure_path(entry)) return false;
    rdt_path_move_to(entry->path, x, y);
    entry->path_has_point = true;
    entry->path_current_x = x;
    entry->path_current_y = y;
    entry->path_subpath_x = x;
    entry->path_subpath_y = y;
    entry->path_has_subpath = true;
    return true;
}

extern "C" bool radiant_canvas_line_to(void* canvas_element, float x, float y) {
    CanvasEntry* entry = canvas_entry_for_element((DomElement*)canvas_element, true);
    if (!canvas_ensure_path(entry)) return false;
    if (!entry->path_has_point) {
        rdt_path_move_to(entry->path, x, y);
        entry->path_subpath_x = x;
        entry->path_subpath_y = y;
        entry->path_has_subpath = true;
    } else rdt_path_line_to(entry->path, x, y);
    entry->path_has_point = true;
    entry->path_current_x = x;
    entry->path_current_y = y;
    return true;
}

extern "C" bool radiant_canvas_bezier_curve_to(void* canvas_element,
                                                   float cp1x, float cp1y,
                                                   float cp2x, float cp2y,
                                                   float x, float y) {
    CanvasEntry* entry = canvas_entry_for_element((DomElement*)canvas_element, true);
    if (!canvas_ensure_path(entry)) return false;
    if (!entry->path_has_point) {
        rdt_path_move_to(entry->path, cp1x, cp1y);
        entry->path_current_x = cp1x;
        entry->path_current_y = cp1y;
        entry->path_subpath_x = cp1x;
        entry->path_subpath_y = cp1y;
        entry->path_has_subpath = true;
    }
    rdt_path_cubic_to(entry->path, cp1x, cp1y, cp2x, cp2y, x, y);
    entry->path_has_point = true;
    entry->path_current_x = x;
    entry->path_current_y = y;
    return true;
}

extern "C" bool radiant_canvas_quadratic_curve_to(void* canvas_element,
                                                       float cpx, float cpy,
                                                       float x, float y) {
    CanvasEntry* entry = canvas_entry_for_element((DomElement*)canvas_element, true);
    if (!canvas_ensure_path(entry)) return false;
    if (!entry->path_has_point) {
        rdt_path_move_to(entry->path, cpx, cpy);
        entry->path_current_x = cpx;
        entry->path_current_y = cpy;
        entry->path_subpath_x = cpx;
        entry->path_subpath_y = cpy;
        entry->path_has_subpath = true;
        entry->path_has_point = true;
    }
    float cp1x = entry->path_current_x + (cpx - entry->path_current_x) * (2.0f / 3.0f);
    float cp1y = entry->path_current_y + (cpy - entry->path_current_y) * (2.0f / 3.0f);
    float cp2x = x + (cpx - x) * (2.0f / 3.0f);
    float cp2y = y + (cpy - y) * (2.0f / 3.0f);
    rdt_path_cubic_to(entry->path, cp1x, cp1y, cp2x, cp2y, x, y);
    entry->path_current_x = x;
    entry->path_current_y = y;
    return true;
}

extern "C" bool radiant_canvas_rect(void* canvas_element, float x, float y,
                                       float width, float height) {
    CanvasEntry* entry = canvas_entry_for_element((DomElement*)canvas_element, true);
    if (!canvas_ensure_path(entry)) return false;
    rdt_path_add_rect(entry->path, x, y, width, height, 0.0f, 0.0f);
    entry->path_has_point = true;
    entry->path_current_x = x;
    entry->path_current_y = y;
    entry->path_subpath_x = x;
    entry->path_subpath_y = y;
    entry->path_has_subpath = true;
    return true;
}

extern "C" bool radiant_canvas_arc(void* canvas_element, float x, float y,
                                      float radius, float start_angle,
                                      float end_angle, bool counter_clockwise) {
    CanvasEntry* entry = canvas_entry_for_element((DomElement*)canvas_element, true);
    if (!canvas_ensure_path(entry) || radius < 0.0f) return false;
    const float two_pi = 6.28318530717958647692f;
    float delta = end_angle - start_angle;
    if (!counter_clockwise) {
        while (delta < 0.0f) delta += two_pi;
        if (delta > two_pi) delta = two_pi;
    } else {
        while (delta > 0.0f) delta -= two_pi;
        if (delta < -two_pi) delta = -two_pi;
    }
    float start_x = x + radius * cosf(start_angle);
    float start_y = y + radius * sinf(start_angle);
    if (entry->path_has_point) rdt_path_line_to(entry->path, start_x, start_y);
    else {
        rdt_path_move_to(entry->path, start_x, start_y);
        entry->path_subpath_x = start_x;
        entry->path_subpath_y = start_y;
        entry->path_has_subpath = true;
    }
    int segments = (int)ceilf(fabsf(delta) / 1.57079632679489661923f); // INT_CAST_OK: arc uses at most four cubic quarters.
    if (segments < 1) segments = 1;
    float step = delta / (float)segments;
    for (int index = 0; index < segments; index++) {
        float a0 = start_angle + step * (float)index;
        float a1 = a0 + step;
        float control = 4.0f / 3.0f * tanf((a1 - a0) * 0.25f);
        float c0 = cosf(a0), s0 = sinf(a0), c1 = cosf(a1), s1 = sinf(a1);
        rdt_path_cubic_to(entry->path, x + radius * (c0 - control * s0),
                          y + radius * (s0 + control * c0),
                          x + radius * (c1 + control * s1),
                          y + radius * (s1 - control * c1),
                          x + radius * c1, y + radius * s1);
    }
    entry->path_has_point = true;
    entry->path_current_x = x + radius * cosf(start_angle + delta);
    entry->path_current_y = y + radius * sinf(start_angle + delta);
    return true;
}

extern "C" bool radiant_canvas_clip(void* canvas_element) {
    CanvasEntry* entry = canvas_entry_for_element((DomElement*)canvas_element, true);
    if (!canvas_ensure_path(entry)) return false;
    CanvasClip* clip = (CanvasClip*)mem_calloc(1, sizeof(CanvasClip), MEM_CAT_LAYOUT);
    if (!clip) return false;
    clip->path = rdt_path_clone(entry->path);
    if (!clip->path) {
        mem_free(clip);
        return false;
    }
    clip->transform = entry->state.transform;
    clip->next = entry->clips;
    entry->clips = clip;
    entry->clip_depth++;
    return true;
}

extern "C" bool radiant_canvas_fill(void* canvas_element) {
    return canvas_draw_path(canvas_entry_for_element((DomElement*)canvas_element, true), false);
}

extern "C" bool radiant_canvas_stroke(void* canvas_element) {
    return canvas_draw_path(canvas_entry_for_element((DomElement*)canvas_element, true), true);
}

extern "C" bool radiant_canvas_fill_rect(void* canvas_element,
                                            float x, float y, float width, float height) {
    return canvas_draw_rect(canvas_entry_for_element((DomElement*)canvas_element, true),
                            x, y, width, height, false);
}

extern "C" bool radiant_canvas_stroke_rect(void* canvas_element,
                                              float x, float y, float width, float height) {
    return canvas_draw_rect(canvas_entry_for_element((DomElement*)canvas_element, true),
                            x, y, width, height, true);
}

extern "C" bool radiant_canvas_clear_rect(void* canvas_element,
                                             float x, float y, float width, float height) {
    CanvasEntry* entry = canvas_entry_for_element((DomElement*)canvas_element, true);
    if (!entry || !entry->surface) return entry != nullptr;
    if (width < 0.0f) {
        x += width;
        width = -width;
    }
    if (height < 0.0f) {
        y += height;
        height = -height;
    }
    int pixel_width = entry->surface->width;
    int pixel_height = entry->surface->height;
    uint32_t* mask_pixels = (uint32_t*)mem_calloc(
        (size_t)pixel_width * (size_t)pixel_height, sizeof(uint32_t), MEM_CAT_LAYOUT);
    if (!mask_pixels) return false;

    RdtPath* rect = rdt_path_new();
    if (!rect) {
        mem_free(mask_pixels);
        return false;
    }
    rdt_path_add_rect(rect, x, y, width, height, 0.0f, 0.0f);
    RdtVector vector = {};
    rdt_vector_init(&vector, mask_pixels, pixel_width, pixel_height, pixel_width);
    int saved_clip_depth = 0;
    int pushed_clips = canvas_push_clips(entry, &vector, &saved_clip_depth);
    // Render the transformed rect through the active clip before clearing pixels.
    rdt_fill_path(&vector, rect, canvas_color(255, 255, 255, 255),
                  RDT_FILL_WINDING, &entry->state.transform);
    canvas_pop_clips(&vector, pushed_clips, saved_clip_depth);
    rdt_vector_destroy(&vector);
    rdt_path_free(rect);

    int stride = entry->surface->pitch / 4;
    uint32_t* pixels = (uint32_t*)entry->surface->pixels;
    for (int py = 0; py < pixel_height; py++) {
        for (int px = 0; px < pixel_width; px++) {
            if ((mask_pixels[py * pixel_width + px] >> 24) != 0) {
                pixels[py * stride + px] = 0;
            }
        }
    }
    mem_free(mask_pixels);
    canvas_note_pixels_changed(entry);
    return true;
}

static float canvas_text_start_x(const CanvasState* state, float x, float width) {
    if (!state) return x;
    if (state->text_align == CANVAS_TEXT_ALIGN_CENTER) return x - width * 0.5f;
    if (state->text_align == CANVAS_TEXT_ALIGN_END ||
        state->text_align == CANVAS_TEXT_ALIGN_RIGHT) return x - width;
    return x;
}

static void canvas_draw_text_glyph(ImageSurface* surface, const GlyphBitmap* bitmap,
                                   float x, float y, const Bound* clip, Color color) {
    if (!surface || !bitmap || !clip) return;
    int glyph_x = (int)lroundf(x); // INT_CAST_OK: glyph bitmap origins address raster pixels.
    int glyph_y = (int)lroundf(y); // INT_CAST_OK: glyph bitmap origins address raster pixels.
    if (bitmap->pixel_mode == GLYPH_PIXEL_BGRA) {
        glyph_draw_color_bgra_bitmap(surface, bitmap, glyph_x, glyph_y, clip);
    } else {
        glyph_draw_coverage_bitmap(surface, bitmap, glyph_x, glyph_y, clip, color);
    }
}

extern "C" bool radiant_canvas_fill_text(void* canvas_element, void* font_handle,
                                            const char* text, int text_len,
                                            float x, float y, float max_width) {
    CanvasEntry* entry = canvas_entry_for_element((DomElement*)canvas_element, true);
    FontHandle* handle = (FontHandle*)font_handle;
    if (!entry || !handle || !text || text_len < 0) return false;
    if (!entry->surface || text_len == 0) return true;

    const char* family = nullptr;
    float size_px = 0.0f;
    FontWeight weight = FONT_WEIGHT_NORMAL;
    FontSlant slant = FONT_SLANT_NORMAL;
    if (!font_handle_get_style(handle, &family, &size_px, &weight, &slant) ||
        !family || size_px <= 0.0f) return false;
    FontStyleDesc style = {};
    style.family = family;
    style.size_px = size_px;
    style.weight = weight;
    style.slant = slant;

    TextExtents extents = font_measure_text(handle, text, text_len);
    float width_scale = 1.0f;
    if (isfinite(max_width) && max_width > 0.0f && extents.width > max_width) {
        width_scale = max_width / extents.width;
    }
    float text_x = canvas_text_start_x(&entry->state, x, extents.width * width_scale);

    ImageSurface* text_surface = image_surface_create(entry->surface->width,
                                                       entry->surface->height);
    if (!text_surface) return false;
    memset(text_surface->pixels, 0,
           (size_t)text_surface->pitch * (size_t)text_surface->height);
    Bound surface_clip = {0.0f, 0.0f, (float)text_surface->width,
                           (float)text_surface->height};
    Color color = entry->state.fill_color;
    float pen_x = text_x;
    uint32_t previous_index = 0;
    for (int offset = 0; offset < text_len;) {
        uint32_t codepoint = 0;
        int consumed = str_utf8_decode(text + offset, (size_t)(text_len - offset),
                                       &codepoint);
        if (consumed <= 0) {
            codepoint = (uint8_t)text[offset];
            consumed = 1;
        }
        offset += consumed;
        uint32_t glyph_index = font_get_glyph_index(handle, codepoint);
        if (previous_index && glyph_index) {
            pen_x += font_get_kerning_by_index(handle, previous_index, glyph_index);
        }
        LoadedGlyph* glyph = font_load_glyph(handle, &style, codepoint, true);
        if (glyph) {
            canvas_draw_text_glyph(text_surface, &glyph->bitmap,
                                   pen_x + (float)glyph->bitmap.bearing_x,
                                   y - (float)glyph->bitmap.bearing_y,
                                   &surface_clip, color);
            pen_x += glyph->advance_x;
        } else {
            pen_x += size_px * 0.5f;
        }
        previous_index = glyph_index;
    }

    RdtMatrix text_scale = {width_scale, 0.0f, text_x - width_scale * text_x,
                             0.0f, 1.0f, 0.0f,
                             0.0f, 0.0f, 1.0f};
    RdtMatrix transform = rdt_matrix_multiply(&entry->state.transform, &text_scale);
    RdtVector vector = {};
    rdt_vector_init(&vector, (uint32_t*)entry->surface->pixels,
                    entry->surface->width, entry->surface->height,
                    entry->surface->pitch / 4);
    int saved_clip_depth = 0;
    int pushed_clips = canvas_push_clips(entry, &vector, &saved_clip_depth);
    // The glyph surface keeps text raster local; Rdt applies canvas transform and clips.
    uint8_t opacity = (uint8_t)(entry->state.global_alpha * 255.0f + 0.5f);
    rdt_draw_image(&vector, (const uint32_t*)text_surface->pixels,
                   text_surface->width, text_surface->height,
                   text_surface->pitch / 4, 0.0f, 0.0f,
                   (float)text_surface->width, (float)text_surface->height,
                   opacity, &transform);
    canvas_pop_clips(&vector, pushed_clips, saved_clip_depth);
    rdt_vector_destroy(&vector);
    image_surface_destroy(text_surface);
    canvas_note_pixels_changed(entry);
    return true;
}

void render_canvas_content(RenderContext* rdcon, ViewBlock* view) {
    if (!rdcon || !view || !view->is_element()) return;
    DomElement* element = lam::dom_require_element(lam::view_dom_node(view));
    CanvasEntry* entry = canvas_entry_for_element(element, false);
    if (!entry || !entry->surface) return;

    Rect rect = render_geometry_block_content_rect(&rdcon->block, view,
                                                   rdcon->raster_scale);
    if (rect.width <= 0.0f || rect.height <= 0.0f) return;
    Bound clip = rdcon->has_transform
        ? rdcon->block.clip
        : view_geometry_intersect_bound_rect(rdcon->block.clip, rect);
    render_painter_blit_surface_scaled(rdcon, entry->surface, nullptr,
                                       rdcon->ui_context->surface, &rect, &clip,
                                       SCALE_MODE_LINEAR, rdcon->clip_shapes,
                                       rdcon->clip_shape_depth, 255);
}
