#include "render.hpp"
#include "svg_animation.hpp"
#include "../lambda/input/css/dom_element.hpp"
#include "../lib/memtrack.h"
#include <limits.h>

// the registry and its programs live in `pool`, which the document's
// resource hook destroys; each program's graph lives in the program's arena.
struct SvgFilterRegistry : DomDocumentResourceData {
    lam::Own<Pool> pool;
    lam::Up<DomDocument> document;
    lam::Up<MemContext> memory;
    MemNode* node;
    uint32_t reclaimer;
    lam::Own<RdtSvgFilterProgram> programs;
};

static void svg_filter_program_free(SvgFilterRegistry* registry, RdtSvgFilterProgram* program) {
    arena_destroy(program->arena);
    pool_free(registry->pool, program);
}

struct RdtSvgFilterNoise {
    unsigned lattice[514];
    double gradient[4][514][2];
};

static int64_t svg_filter_random(int64_t seed) {
    int64_t value = 16807 * (seed % 127773) - 2836 * (seed / 127773);
    return value <= 0 ? value + 2147483647 : value;
}

RdtSvgFilterNoise* render_svg_filter_noise_create(Arena* arena, MemContext* memory, float value) {
    if (!arena || !isfinite(value)) return nullptr;
    if (!render_memory_allow_allocation(memory, sizeof(RdtSvgFilterNoise) + ARENA_MAX_CHUNK_SIZE)) return nullptr;
    RdtSvgFilterNoise* noise = (RdtSvgFilterNoise*)arena_calloc(arena, sizeof(*noise));
    if (!noise) return nullptr;
    double normalized = trunc((double)value);
    normalized = normalized <= 0.0 ? -fmod(normalized, 2147483646.0) + 1.0 : fmin(normalized, 2147483646.0);
    int64_t seed = (int64_t)normalized; // INT_CAST_OK: normalized pseudo-random generator state
    // Filter Effects §9.21 fixes channel order and the seed sequence, so tiles are independent of traversal/cache state.
    for (unsigned channel = 0; channel < 4; channel++) for (unsigned index = 0; index < 256; index++) {
        noise->lattice[index] = index;
        double* gradient = noise->gradient[channel][index], length;
        do {
            for (unsigned axis = 0; axis < 2; axis++) {
                seed = svg_filter_random(seed); gradient[axis] = (double)(seed % 512 - 256) / 256.0;
            }
            length = hypot(gradient[0], gradient[1]);
        } while (length == 0.0 || length > 1.0);
        gradient[0] /= length; gradient[1] /= length;
    }
    for (unsigned index = 255; index > 0; index--) {
        seed = svg_filter_random(seed); unsigned other = (unsigned)(seed % 256);
        unsigned swap = noise->lattice[index]; noise->lattice[index] = noise->lattice[other]; noise->lattice[other] = swap;
    }
    for (unsigned index = 0; index < 258; index++) {
        noise->lattice[256 + index] = noise->lattice[index];
        for (unsigned channel = 0; channel < 4; channel++) for (unsigned axis = 0; axis < 2; axis++)
            noise->gradient[channel][256 + index][axis] = noise->gradient[channel][index][axis];
    }
    return noise;
}

struct SvgFilterNoiseFrame {
    double frequency[2], width[2], wrap[2];
    bool stitch;
};

static SvgFilterNoiseFrame svg_filter_noise_frame(const RdtSvgFilterNode* node, Bound region, float scale_x, float scale_y, Bound geometry, bool bbox) {
    SvgFilterNoiseFrame frame = {};
    frame.frequency[0] = node->values[0] / scale_x; frame.frequency[1] = node->values[1] / scale_y;
    frame.stitch = (node->variant & 2u) != 0;
    if (frame.stitch) for (unsigned axis = 0; axis < 2; axis++) {
        double extent = axis ? region.bottom - region.top : region.right - region.left;
        double origin = (axis ? region.top : region.left) - (bbox ? (axis ? geometry.top : geometry.left) : 0.0f), frequency = frame.frequency[axis];
        if (frequency > 0.0) {
            double low = floor(extent * frequency) / extent, high = ceil(extent * frequency) / extent;
            frame.frequency[axis] = low > 0.0 && frequency / low < high / frequency ? low : high;
        }
        frame.width[axis] = floor(extent * frame.frequency[axis] + .5);
        frame.wrap[axis] = trunc(origin * frame.frequency[axis] + 4096.0 + frame.width[axis]);
    }
    return frame;
}

static double svg_filter_noise_sample(const RdtSvgFilterNoise* noise, unsigned channel, double x, double y, const SvgFilterNoiseFrame* frame) {
    const double point[] = {x + 4096.0, y + 4096.0};
    unsigned lattice[2][2]; double fraction[2];
    for (unsigned axis = 0; axis < 2; axis++) {
        double start = floor(point[axis]); fraction[axis] = point[axis] - start;
        for (unsigned side = 0; side < 2; side++) {
            double index = start + side;
            if (frame->stitch && index >= frame->wrap[axis]) index -= frame->width[axis];
            double wrapped = fmod(index, 256.0); if (wrapped < 0.0) wrapped += 256.0;
            lattice[axis][side] = (unsigned)wrapped;
        }
    }
    double rows[2];
    double curve_x = fraction[0] * fraction[0] * (3.0 - 2.0 * fraction[0]);
    double curve_y = fraction[1] * fraction[1] * (3.0 - 2.0 * fraction[1]);
    for (unsigned row = 0; row < 2; row++) {
        double dots[2];
        for (unsigned column = 0; column < 2; column++) {
            unsigned index = noise->lattice[noise->lattice[lattice[0][column]] + lattice[1][row]];
            const double* gradient = noise->gradient[channel][index];
            dots[column] = (fraction[0] - column) * gradient[0] + (fraction[1] - row) * gradient[1];
        }
        rows[row] = dots[0] + curve_x * (dots[1] - dots[0]);
    }
    return rows[0] + curve_y * (rows[1] - rows[0]);
}

static uint32_t svg_filter_noise_pixel(const RdtSvgFilterNode* node, SvgFilterNoiseFrame frame, float x, float y) {
    double sums[4] = {}, px = x * frame.frequency[0], py = y * frame.frequency[1], amplitude = 1.0;
    unsigned octaves = (unsigned)node->values[2];
    for (unsigned octave = 0; octave < octaves; octave++) {
        for (unsigned channel = 0; channel < 4; channel++) {
            double value = svg_filter_noise_sample(node->noise, channel, px, py, &frame);
            sums[channel] += ((node->variant & 1u) ? value : fabs(value)) * amplitude;
        }
        px *= 2.0; py *= 2.0; amplitude *= .5;
        if (frame.stitch) for (unsigned axis = 0; axis < 2; axis++) { frame.width[axis] *= 2.0; frame.wrap[axis] = 2.0 * frame.wrap[axis] - 4096.0; }
    }
    float values[4];
    for (unsigned channel = 0; channel < 4; channel++) values[channel] = clamp_unit((float)((node->variant & 1u) ? (sums[channel] + 1.0) * .5 : sums[channel]));
    for (unsigned channel = 0; channel < 3; channel++) values[channel] *= values[3];
    return render_pixel_pack_abgr(clamp_byte_round(values[0] * 255), clamp_byte_round(values[1] * 255),
        clamp_byte_round(values[2] * 255), clamp_byte_round(values[3] * 255));
}

static size_t svg_filter_program_bytes(const RdtSvgFilterProgram* program) {
    return sizeof(*program) + arena_total_allocated(program->arena);
}

static bool svg_filter_registry_stat(void* data, MemStatSample* sample) {
    SvgFilterRegistry* registry = (SvgFilterRegistry*)data;
    sample->bytes_reserved = sample->bytes_in_use = sizeof(*registry);
    for (RdtSvgFilterProgram* program = registry->programs; program; program = program->next) {
        sample->bytes_reserved += svg_filter_program_bytes(program);
        sample->bytes_in_use += sizeof(*program) + arena_total_used(program->arena);
        sample->alloc_count++;
    }
    return true;
}

static size_t svg_filter_registry_reclaim(MemPressureLevel, size_t target, void* data) {
    SvgFilterRegistry* registry = (SvgFilterRegistry*)data;
    size_t freed = 0;
    lam::Own<RdtSvgFilterProgram>* cursor = &registry->programs;
    while (*cursor && (!target || freed < target)) {
        RdtSvgFilterProgram* program = *cursor;
        if (program->active_users) { cursor = &program->next; continue; }
        *cursor = program->next;
        freed += svg_filter_program_bytes(program);
        // the registry is the sole memory-context node; reclaiming its raw arenas does not reenter the coordinator.
        svg_filter_program_free(registry, program);
    }
    return freed;
}

static void svg_filter_registry_destroy(DomDocumentResourceData* data) {
    SvgFilterRegistry* registry = (SvgFilterRegistry*)data;
    mem_context_unregister_reclaimer(registry->reclaimer);
    svg_filter_registry_reclaim(MEM_PRESSURE_CRITICAL, 0, registry);
    mem_unregister(registry->node);
    registry->document->services.svg_filter_registry = nullptr;
    // the registry itself lives in this pool
    mem_pool_destroy(registry->pool);
}

RdtSvgFilterProgram* render_svg_filter_program_acquire(DomDocument* document, Element* element) {
    if (!document || !element) return nullptr;
    SvgFilterRegistry* registry = (SvgFilterRegistry*)document->services.svg_filter_registry;
    if (!registry) {
        MemContext* memory = document->services.mem_ctx ? (MemContext*)document->services.mem_ctx : mem_context_root();
        Pool* pool = mem_pool_create(memory, MEM_ROLE_RENDER, "render.svg.filter_registry");
        if (!pool) return nullptr;
        registry = (SvgFilterRegistry*)pool_calloc(pool, sizeof(*registry));
        // the document takes ownership once its resource hook is registered
        if (!registry || !dom_document_add_resource(document, registry, svg_filter_registry_destroy)) {
            mem_pool_destroy(pool); return nullptr;
        }
        registry->pool = lam::own(pool);
        registry->document = lam::up(document); registry->memory = lam::up(memory);
        registry->node = mem_register(registry->memory, MEM_KIND_CACHE, MEM_ROLE_RENDER,
            "render.svg.filter_programs", registry, nullptr, svg_filter_registry_stat, nullptr);
        registry->reclaimer = mem_context_register_reclaimer(registry->memory,
            svg_filter_registry_reclaim, registry, 0);
        document->services.svg_filter_registry = registry;
    }
    uint64_t animation_generation = svg_animation_source_generation(document, element);
    for (RdtSvgFilterProgram* program = registry->programs; program; program = program->next)
        if (program->element == element && program->epoch == document->mutation_epoch &&
            program->animation_generation == animation_generation && !program->allocation_failed) {
            program->active_users++; return program;
        }
    if (!render_memory_allow_allocation(registry->memory, sizeof(RdtSvgFilterProgram) + ARENA_INITIAL_CHUNK_SIZE)) return nullptr;
    RdtSvgFilterProgram* program = (RdtSvgFilterProgram*)pool_calloc(registry->pool, sizeof(*program));
    if (!program) return nullptr;
    program->arena = lam::own(arena_create_default());
    if (!program->arena) { pool_free(registry->pool, program); return nullptr; }
    arena_set_mem_category(program->arena, MEM_CAT_RENDER);
    program->element = lam::up(element); program->epoch = document->mutation_epoch; program->active_users = 1;
    // clock-only SMIL changes alter compiled resource facts without changing the DOM epoch.
    program->animation_generation = animation_generation;
    program->next = registry->programs; registry->programs = lam::own(program);
    // stale, unpinned programs are retired on mutation rather than accumulating until document teardown.
    lam::Own<RdtSvgFilterProgram>* cursor = &registry->programs;
    while (*cursor) {
        RdtSvgFilterProgram* old = *cursor;
        if ((old->epoch != program->epoch || old->animation_generation != animation_generation ||
            old->allocation_failed) && !old->active_users) {
            *cursor = old->next; svg_filter_program_free(registry, old);
        } else cursor = &old->next;
    }
    return program;
}

void render_svg_filter_program_release(RdtSvgFilterProgram* program) {
    if (program && program->active_users) program->active_users--;
}

int render_svg_filter_input(const RdtSvgFilterProgram* program, size_t before, const char* name) {
    if (!name || !*name) return before ? (int)(before - 1) : RDT_SVG_FILTER_SOURCE; // INT_CAST_OK: bounded graph node index
    static const char* const names[] = {"SourceGraphic", "SourceAlpha", "FillPaint", "StrokePaint", "BackgroundImage", "BackgroundAlpha"};
    static const int indices[] = {RDT_SVG_FILTER_SOURCE, RDT_SVG_FILTER_ALPHA, RDT_SVG_FILTER_FILL,
        RDT_SVG_FILTER_STROKE, RDT_SVG_FILTER_BACKGROUND, RDT_SVG_FILTER_BACKGROUND_ALPHA};
    for (size_t index = 0; index < 6; index++) if (strcmp(name, names[index]) == 0) return indices[index];
    for (size_t index = before; index > 0; index--)
        if (program->nodes[index - 1].result && strcmp(name, program->nodes[index - 1].result) == 0)
            return (int)(index - 1); // INT_CAST_OK: bounded graph node index
    // unknown/forward result names use the same implicit input as an omitted `in` (Filter Effects §9.2).
    return before ? (int)(before - 1) : RDT_SVG_FILTER_SOURCE; // INT_CAST_OK: bounded graph node index
}

static Bound svg_filter_intersect(Bound a, Bound b) {
    return {fmaxf(a.left, b.left), fmaxf(a.top, b.top), fminf(a.right, b.right), fminf(a.bottom, b.bottom)};
}

static Bound svg_filter_union(Bound a, Bound b) {
    return {fminf(a.left, b.left), fminf(a.top, b.top), fmaxf(a.right, b.right), fmaxf(a.bottom, b.bottom)};
}

static bool svg_filter_nonempty(Bound box) {
    return isfinite(box.left) && isfinite(box.top) && isfinite(box.right) && isfinite(box.bottom) &&
        box.right > box.left && box.bottom > box.top;
}

static Bound svg_filter_resolve_region(const lam::Own<const char>* tokens, const SvgLengthContext* lengths,
    Bound defaults, Bound basis, bool bbox) {
    SvgLengthContext context = *lengths;
    if (bbox) context.viewport_width = context.viewport_height = 1.0f;
    float values[4] = {defaults.left, defaults.top, defaults.right - defaults.left, defaults.bottom - defaults.top};
    for (size_t index = 0; index < 4; index++) if (tokens[index]) {
        values[index] = svg_resolve_length(tokens[index], &context, index % 2 ? SVG_LENGTH_Y : SVG_LENGTH_X, 0.0f);
        if (bbox) values[index] = values[index] * (index % 2 ? basis.bottom - basis.top : basis.right - basis.left) +
            (index < 2 ? (index % 2 ? basis.top : basis.left) : 0.0f);
    }
    return {values[0], values[1], values[0] + values[2], values[1] + values[3]};
}

static SvgLengthContext svg_filter_resource_lengths(const RdtSvgFilterRun* run, Element* resource) {
    SvgLengthContext lengths = run->lengths;
    if (run->resolve_lengths && resource) run->resolve_lengths(run->image_context, resource, &lengths);
    return lengths;
}

bool render_svg_filter_region(const RdtSvgFilterProgram* program, const RdtSvgFilterRun* run, Bound* region) {
    if (!program || !run || !region || !program->valid) return false;
    if (program->filter_bbox && !svg_filter_nonempty(run->geometry)) return false;
    Bound basis = program->filter_bbox ? run->geometry : Bound{0, 0, run->lengths.viewport_width, run->lengths.viewport_height};
    float width = basis.right - basis.left, height = basis.bottom - basis.top;
    Bound defaults = {basis.left - .1f * width, basis.top - .1f * height, basis.right + .1f * width, basis.bottom + .1f * height};
    SvgLengthContext lengths = svg_filter_resource_lengths(run, program->element);
    *region = svg_filter_resolve_region(program->region, &lengths, defaults, run->geometry, program->filter_bbox);
    return svg_filter_nonempty(*region);
}

struct SvgFilterImage {
    ImageSurface* surface;
    Bound region;
    bool linear;
};

struct SvgFilterExecution {
    const RdtSvgFilterRun* run;
    SvgFilterImage standard[4];
    SvgFilterImage* images;
    size_t* references;
    Bound region, grid;
    size_t work, source_index;
};

typedef void (*SvgFilterInputFn)(int input, SvgFilterExecution* execution);
static int svg_filter_standard_slot(int input) {
    switch (input) {
    case RDT_SVG_FILTER_SOURCE: case RDT_SVG_FILTER_ALPHA: return 0;
    case RDT_SVG_FILTER_FILL: return 1;
    case RDT_SVG_FILTER_STROKE: return 2;
    case RDT_SVG_FILTER_BACKGROUND: case RDT_SVG_FILTER_BACKGROUND_ALPHA: return 3;
    default: return -1;
    }
}
static void svg_filter_visit_inputs(const RdtSvgFilterNode* node, SvgFilterInputFn visit, SvgFilterExecution* execution) {
    if (node->kind == RDT_SVG_FILTER_FLOOD || node->kind == RDT_SVG_FILTER_IMAGE || node->kind == RDT_SVG_FILTER_TURBULENCE) return;
    if (node->kind == RDT_SVG_FILTER_MERGE) {
        for (size_t index = 0; index < node->merge_count; index++) visit(node->merge_inputs[index], execution);
        return;
    }
    visit(node->input, execution);
    if (node->kind == RDT_SVG_FILTER_BLEND || node->kind == RDT_SVG_FILTER_COMPOSITE || node->kind == RDT_SVG_FILTER_DISPLACEMENT) visit(node->input2, execution);
}

static void svg_filter_count_input(int input, SvgFilterExecution* execution) {
    if (input >= 0) execution->references[input]++;
    else { int slot = svg_filter_standard_slot(input); if (slot >= 0) execution->references[execution->source_index + (size_t)slot]++; }
}

static void svg_filter_retire_input(int input, SvgFilterExecution* execution) {
    int standard = svg_filter_standard_slot(input);
    if (input < 0 && standard < 0) return;
    size_t slot = input < 0 ? execution->source_index + (size_t)standard : (size_t)input;
    SvgFilterImage* image = input < 0 ? &execution->standard[standard] : &execution->images[input];
    if (execution->references[slot] && --execution->references[slot] == 0 && image->surface) {
        image_surface_destroy(image->surface); image->surface = nullptr;
    }
}

static uint32_t svg_filter_convert(uint32_t pixel, bool source_linear, bool target_linear) {
    if (source_linear == target_linear) return pixel;
    unsigned alpha = pixel >> 24;
    if (!alpha) return 0;
    uint32_t value = alpha << 24;
    for (unsigned channel = 0; channel < 3; channel++) {
        float color = (float)((pixel >> (channel * 8)) & 255u) / (float)alpha;
        color = target_linear ? render_color_srgb_to_linear(color) : render_color_linear_to_srgb(color);
        value |= (uint32_t)clamp_byte_round(color * (float)alpha) << (channel * 8);
    }
    return value;
}

bool render_svg_filter_spend_work(const RdtSvgFilterRun* run, size_t work) {
    if (!run || !run->work_used) return true;
    size_t used = *run->work_used;
    if (work > SIZE_MAX - used || (run->work_limit && (used > run->work_limit || work > run->work_limit - used))) return false;
    *run->work_used = used + work; return true;
}

static bool svg_filter_spend(SvgFilterExecution* execution, size_t work) {
    return render_svg_filter_spend_work(execution->run, work);
}

static ImageSurface* svg_filter_surface(SvgFilterExecution* execution) {
    float width = execution->grid.right - execution->grid.left, height = execution->grid.bottom - execution->grid.top;
    return render_surface_create_budgeted(execution->run->memory, width, height);
}

static const SvgFilterImage* svg_filter_image(const SvgFilterExecution* execution, int input) {
    int standard = svg_filter_standard_slot(input);
    return input >= 0 ? &execution->images[input] : standard >= 0 ? &execution->standard[standard] : nullptr;
}

static uint32_t svg_filter_sample(const SvgFilterExecution* execution, int input, float x, float y, bool linear) {
    const SvgFilterImage* image = svg_filter_image(execution, input);
    if (!image || !image->surface) return 0;
    float ux = (x + execution->grid.left + .5f) / execution->run->density;
    float uy = (y + execution->grid.top + .5f) / execution->run->density;
    if (ux < image->region.left || uy < image->region.top || ux >= image->region.right || uy >= image->region.bottom ||
        x < -.5f || y < -.5f || x >= (float)image->surface->width - .5f || y >= (float)image->surface->height - .5f) return 0;
    uint32_t pixel = render_pixel_sample_bilinear((uint8_t*)image->surface->pixels, image->surface->width,
        image->surface->height, image->surface->pitch, x, y, false, true);
    return input == RDT_SVG_FILTER_ALPHA || input == RDT_SVG_FILTER_BACKGROUND_ALPHA ? pixel & 0xff000000u : svg_filter_convert(pixel, image->linear, linear);
}

static uint32_t svg_filter_sample_tile(const SvgFilterExecution* execution, int input, float x, float y, bool linear) {
    const SvgFilterImage* image = svg_filter_image(execution, input);
    if (!image || !svg_filter_nonempty(image->region)) return 0;
    float origin[] = {image->region.left, image->region.top}, end[] = {image->region.right, image->region.bottom};
    float point[] = {x, y}, grid[] = {execution->grid.left, execution->grid.top};
    float samples[2][2], fraction[2];
    for (unsigned axis = 0; axis < 2; axis++) {
        float period = (end[axis] - origin[axis]) * execution->run->density;
        float start = origin[axis] * execution->run->density - grid[axis] - .5f;
        float low = ceilf(start), high = ceilf(start + period) - 1.0f;
        if (!isfinite(period) || period <= 0.0f || high < low) return 0;
        float offset = fmodf(point[axis] - origin[axis], end[axis] - origin[axis]);
        if (offset < 0.0f) offset += end[axis] - origin[axis];
        float position = start + offset * execution->run->density;
        float before = floorf(position), after = before + 1.0f;
        // interpolate between opposite border samples, retaining the declared fractional repeat period (§9.20).
        if (position < low) { before = high - period; after = low; samples[axis][0] = high; samples[axis][1] = low; }
        else if (before >= high) { before = high; after = low + period; samples[axis][0] = high; samples[axis][1] = low; }
        else { samples[axis][0] = before; samples[axis][1] = after; }
        fraction[axis] = after > before ? clamp_unit((position - before) / (after - before)) : 0.0f;
    }
    float channels[4] = {};
    for (unsigned row = 0; row < 2; row++) for (unsigned column = 0; column < 2; column++) {
        uint32_t pixel = svg_filter_sample(execution, input, samples[0][column], samples[1][row], linear);
        float weight = (column ? fraction[0] : 1.0f - fraction[0]) * (row ? fraction[1] : 1.0f - fraction[1]);
        for (unsigned channel = 0; channel < 4; channel++) channels[channel] += weight * (float)((pixel >> (channel * 8)) & 255u);
    }
    return render_pixel_pack_abgr(clamp_byte_round(channels[0]), clamp_byte_round(channels[1]),
        clamp_byte_round(channels[2]), clamp_byte_round(channels[3]));
}

static uint32_t svg_filter_pack(const float values[4]) {
    uint8_t alpha = clamp_byte_round(clamp_unit(values[3]) * 255.0f);
    return render_pixel_pack_abgr(clamp_byte_round(fminf(clamp_unit(values[0]), (float)alpha / 255.0f) * 255.0f),
        clamp_byte_round(fminf(clamp_unit(values[1]), (float)alpha / 255.0f) * 255.0f),
        clamp_byte_round(fminf(clamp_unit(values[2]), (float)alpha / 255.0f) * 255.0f), alpha);
}

static void svg_filter_unpack(uint32_t pixel, float values[4]) {
    for (unsigned channel = 0; channel < 4; channel++) values[channel] = (float)((pixel >> (channel * 8)) & 255u) / 255.0f;
}

static uint32_t svg_filter_composite(uint32_t a, uint32_t b, unsigned operation, const float* coefficients) {
    float first[4], second[4], result[4]; svg_filter_unpack(a, first); svg_filter_unpack(b, second);
    float fa = 1.0f, fb = 1.0f - first[3];
    switch (operation) {
        case 1: fa = second[3]; fb = 0.0f; break;
        case 2: fa = 1.0f - second[3]; fb = 0.0f; break;
        case 3: fa = second[3]; fb = 1.0f - first[3]; break;
        case 4: fa = 1.0f - second[3]; fb = 1.0f - first[3]; break;
        case 6: fb = 1.0f; break;
    }
    for (unsigned channel = 0; channel < 4; channel++) result[channel] = operation == 5
        ? coefficients[0] * first[channel] * second[channel] + coefficients[1] * first[channel] +
          coefficients[2] * second[channel] + coefficients[3]
        : first[channel] * fa + second[channel] * fb;
    return svg_filter_pack(result);
}

static ImageSurface* svg_filter_copy_input(SvgFilterExecution* execution, int input, bool linear) {
    ImageSurface* current = svg_filter_surface(execution);
    if (!current) return nullptr;
    size_t pixels = (size_t)current->width * (size_t)current->height;
    if (!svg_filter_spend(execution, pixels)) { image_surface_destroy(current); return nullptr; }
    uint32_t* data = (uint32_t*)current->pixels;
    for (int row = 0; row < current->height; row++) for (int column = 0; column < current->width; column++)
        data[(size_t)row * (size_t)current->width + (size_t)column] = svg_filter_sample(execution, input, (float)column, (float)row, linear);
    return current;
}

static int svg_filter_edge_index(int64_t coordinate, int low, int high, unsigned mode) {
    if (high <= low) return -1;
    if (!mode) return coordinate >= low && coordinate < high ? (int)coordinate : -1; // INT_CAST_OK: bounded raster sample index
    if (mode == 1) return coordinate < low ? low : coordinate >= high ? high - 1 : (int)coordinate; // INT_CAST_OK: bounded raster sample index
    int64_t period = high - low, wrapped = (coordinate - low) % period;
    return low + (int)(wrapped < 0 ? wrapped + period : wrapped); // INT_CAST_OK: wrapped raster index within the input extent
}

static ImageSurface* svg_filter_blur(SvgFilterExecution* execution, int input, float sigma_x, float sigma_y, bool linear, unsigned edge = 0) {
    ImageSurface* current = svg_filter_copy_input(execution, input, linear);
    if (!current) return nullptr;
    size_t pixels = (size_t)current->width * (size_t)current->height;
    uint32_t* data = (uint32_t*)current->pixels;
    const float sigmas[] = {sigma_x, sigma_y};
    const SvgFilterImage* source = svg_filter_image(execution, input);
    Bound region = source ? source->region : execution->region;
    for (unsigned axis = 0; axis < 2; axis++) {
        float sigma = sigmas[axis] * execution->run->density;
        if (sigma <= 0.0f) continue;
        float radius_value = ceilf(sigma * 3.0f);
        if (!isfinite(radius_value) || radius_value > (float)(INT_MAX / 2) ||
            (double)pixels * (2.0 * radius_value + 1.0) > (double)SIZE_MAX) { image_surface_destroy(current); return nullptr; }
        int radius = (int)radius_value; // INT_CAST_OK: bounded raster kernel tap count
        float grid_origin = axis ? execution->grid.top : execution->grid.left;
        float extent = axis ? (float)current->height : (float)current->width;
        float region_low = ceilf((axis ? region.top : region.left) * execution->run->density - grid_origin - .5f);
        float region_high = ceilf((axis ? region.bottom : region.right) * execution->run->density - grid_origin - .5f);
        // extension uses the input subregion, not the larger output/filter region (Filter Effects §9.14).
        int low = edge ? (int)fmaxf(0.0f, fminf(extent, region_low)) : 0; // INT_CAST_OK: bounded raster sample index
        int high = edge ? (int)fmaxf(0.0f, fminf(extent, region_high)) : (axis ? current->height : current->width); // INT_CAST_OK: bounded raster sample index
        size_t taps = (size_t)radius * 2 + 1;
        if (!svg_filter_spend(execution, pixels * taps)) { image_surface_destroy(current); return nullptr; }
        // each separable pass releases its kernel through the incoming LIFO scratch contract.
        ScratchScope kernel_scope(execution->run->scratch);
        float* weights = kernel_scope.array<float>(taps);
        ImageSurface* output = svg_filter_surface(execution);
        if (!weights || !output) { if (output) image_surface_destroy(output); image_surface_destroy(current); return nullptr; }
        float sum = 0.0f;
        for (int tap = -radius; tap <= radius; tap++) { float weight = expf(-.5f * (float)tap * (float)tap / (sigma * sigma)); weights[tap + radius] = weight; sum += weight; }
        for (size_t tap = 0; tap < taps; tap++) weights[tap] /= sum;
        uint32_t* destination = (uint32_t*)output->pixels;
        for (int row = 0; row < current->height; row++) for (int column = 0; column < current->width; column++) {
            float values[4] = {};
            for (int tap = -radius; tap <= radius; tap++) {
                int sample = svg_filter_edge_index((int64_t)(axis ? row : column) + tap, low, high, edge);
                int px = axis ? column : sample, py = axis ? sample : row;
                if (px < 0 || py < 0 || px >= current->width || py >= current->height) continue;
                uint32_t pixel = data[(size_t)py * (size_t)current->width + (size_t)px];
                for (unsigned channel = 0; channel < 4; channel++) values[channel] +=
                    (float)((pixel >> (channel * 8)) & 255u) / 255.0f * weights[tap + radius];
            }
            destination[(size_t)row * (size_t)current->width + (size_t)column] = svg_filter_pack(values);
        }
        image_surface_destroy(current); current = output; data = destination;
    }
    return current;
}

static ImageSurface* svg_filter_morphology(SvgFilterExecution* execution, int input, float radius_x, float radius_y, bool dilate, bool linear) {
    ImageSurface* current = svg_filter_copy_input(execution, input, linear);
    if (!current) return nullptr;
    const float radii[] = {radius_x, radius_y};
    for (unsigned axis = 0; axis < 2; axis++) {
        float radius_value = floorf(radii[axis] * execution->run->density);
        if (radius_value <= 0.0f) continue;
        int length = axis ? current->height : current->width, lines = axis ? current->width : current->height;
        if (!isfinite(radius_value)) { image_surface_destroy(current); return nullptr; }
        // windows larger than the grid have identical extrema; cap indices without changing the result.
        bool outside_all = radius_value >= (float)length;
        int radius = outside_all ? length : (int)radius_value; // INT_CAST_OK: bounded physical raster kernel radius
        size_t pixels = (size_t)length * (size_t)lines;
        if (pixels > SIZE_MAX / 4 || !svg_filter_spend(execution, pixels * 4)) { image_surface_destroy(current); return nullptr; }
        ScratchScope window_scope(execution->run->scratch);
        int* deque = window_scope.array<int>((size_t)length);
        ImageSurface* output = svg_filter_surface(execution);
        if (!deque || !output) { if (output) image_surface_destroy(output); image_surface_destroy(current); return nullptr; }
        uint32_t* source = (uint32_t*)current->pixels, *destination = (uint32_t*)output->pixels;
        // separable monotonic windows bound morphology work independently of the requested radius.
        for (int line = 0; line < lines; line++) for (unsigned channel = 0; channel < 4; channel++) {
            size_t head = 0, tail = 0; int added = -1;
            auto offset = [&](int position) -> size_t { return axis ? (size_t)position * (size_t)current->width + (size_t)line : (size_t)line * (size_t)current->width + (size_t)position; };
            auto value = [&](int position) -> uint32_t { return (source[offset(position)] >> (channel * 8)) & 255u; };
            for (int position = 0; position < length; position++) {
                int upper = radius >= length - position ? length - 1 : position + radius;
                while (added < upper) {
                    added++;
                    while (tail > head && (dilate ? value(deque[tail - 1]) <= value(added) : value(deque[tail - 1]) >= value(added))) tail--;
                    deque[tail++] = added;
                }
                while (tail > head && deque[head] < position - radius) head++;
                bool outside = outside_all || position < radius || radius >= length - position;
                uint32_t component = !dilate && outside ? 0 : value(deque[head]);
                destination[offset(position)] |= component << (channel * 8);
            }
        }
        image_surface_destroy(current); current = output;
    }
    return current;
}

static Bound svg_filter_input_region(const SvgFilterExecution* execution, int input) {
    return input < 0 ? execution->region : execution->images[input].region;
}

static Bound svg_filter_node_region(const RdtSvgFilterProgram* program, const RdtSvgFilterNode* node, SvgFilterExecution* execution) {
    Bound defaults = execution->region;
    if (node->kind != RDT_SVG_FILTER_FLOOD && node->kind != RDT_SVG_FILTER_IMAGE && node->kind != RDT_SVG_FILTER_TURBULENCE && node->kind != RDT_SVG_FILTER_TILE) {
        defaults = svg_filter_input_region(execution, node->input);
        if (node->kind == RDT_SVG_FILTER_BLEND || node->kind == RDT_SVG_FILTER_COMPOSITE || node->kind == RDT_SVG_FILTER_DISPLACEMENT)
            defaults = svg_filter_union(defaults, svg_filter_input_region(execution, node->input2));
        if (node->kind == RDT_SVG_FILTER_MERGE) {
            defaults = node->merge_count ? svg_filter_input_region(execution, node->merge_inputs[0]) : Bound{};
            for (size_t input = 1; input < node->merge_count; input++) defaults = svg_filter_union(defaults, svg_filter_input_region(execution, node->merge_inputs[input]));
        }
    }
    // preserve the declared region for image fitting and tile periods; the grid independently clips stored pixels.
    SvgLengthContext lengths = svg_filter_resource_lengths(execution->run, node->element);
    return svg_filter_resolve_region(node->region, &lengths,
        defaults, execution->run->geometry, program->primitive_bbox);
}

static float svg_filter_primitive_scale(const RdtSvgFilterProgram* program, const RdtSvgFilterRun* run, unsigned axis) {
    return !program->primitive_bbox ? 1.0f : axis ? run->geometry.bottom - run->geometry.top : run->geometry.right - run->geometry.left;
}

static bool svg_filter_normalize(double vector[3]) {
    double length = hypot(hypot(vector[0], vector[1]), vector[2]);
    if (!(length > 0.0) || !isfinite(length)) { vector[0] = vector[1] = vector[2] = 0.0; return false; }
    for (unsigned axis = 0; axis < 3; axis++) vector[axis] /= length;
    return true;
}

static uint32_t svg_filter_lighting(const RdtSvgFilterProgram* program, const RdtSvgFilterNode* node,
    SvgFilterExecution* execution, float x, float y, uint32_t color) {
    const RdtSvgFilterLight* light = &node->light;
    const RdtSvgFilterRun* run = execution->run;
    float scale[] = {svg_filter_primitive_scale(program, run, 0), svg_filter_primitive_scale(program, run, 1)};
    double delta[2], step[2];
    for (unsigned axis = 0; axis < 2; axis++) {
        delta[axis] = light->kernel[axis] > 0.0f ? light->kernel[axis] : 1.0 / ((double)run->density * scale[axis]);
        step[axis] = delta[axis] * scale[axis] * run->density;
    }
    const SvgFilterImage* input = svg_filter_image(execution, node->input);
    Bound region = input ? svg_filter_intersect(input->region, execution->region) : execution->region;
    float point[] = {x, y};
    unsigned low[2], high[2];
    for (unsigned axis = 0; axis < 2; axis++) {
        double coordinate = (point[axis] + (axis ? execution->grid.top : execution->grid.left) + .5) / run->density;
        double start = axis ? region.top : region.left, end = axis ? region.bottom : region.right;
        low[axis] = coordinate - delta[axis] * scale[axis] >= start ? 0u : 1u;
        high[axis] = coordinate + delta[axis] * scale[axis] < end ? 2u : 1u;
    }
    double alpha[3][3] = {};
    for (unsigned row = low[1]; row <= high[1]; row++) for (unsigned column = low[0]; column <= high[0]; column++)
        alpha[row][column] = (double)(svg_filter_sample(execution, node->input,
            x + (float)(((double)column - 1.0) * step[0]), y + (float)(((double)row - 1.0) * step[1]), false) >> 24) / 255.0;
    double normal[] = {0.0, 0.0, 1.0};
    // one weighted endpoint difference yields all nine Sobel boundary kernels (Filter Effects §9.10).
    for (unsigned axis = 0; axis < 2; axis++) {
        unsigned transverse = 1 - axis; double slope = 0.0, weight_sum = 0.0;
        for (unsigned offset = low[transverse]; offset <= high[transverse]; offset++) {
            double weight = offset == 1 ? 2.0 : 1.0; weight_sum += weight;
            slope += weight * (axis ? alpha[high[axis]][offset] - alpha[low[axis]][offset] :
                alpha[offset][high[axis]] - alpha[offset][low[axis]]);
        }
        if (high[axis] > low[axis]) normal[axis] = -2.0 * light->surface_scale * slope /
            ((high[axis] - low[axis]) * weight_sum * delta[axis]);
    }
    svg_filter_normalize(normal);
    double direction[3];
    for (unsigned axis = 0; axis < 3; axis++) direction[axis] = light->position[axis];
    double distance = 0.0;
    if (light->kind != 1) {
        direction[0] -= ((x + execution->grid.left + .5) / run->density - (program->primitive_bbox ? run->geometry.left : 0.0)) / scale[0];
        direction[1] -= ((y + execution->grid.top + .5) / run->density - (program->primitive_bbox ? run->geometry.top : 0.0)) / scale[1];
        direction[2] -= light->surface_scale * alpha[1][1];
        distance = hypot(hypot(direction[0], direction[1]), direction[2]);
        svg_filter_normalize(direction);
    }
    double attenuation = 1.0;
    if (light->kind == 3) {
        double spot[3];
        for (unsigned axis = 0; axis < 3; axis++) spot[axis] = (double)light->target[axis] - light->position[axis];
        bool valid = svg_filter_normalize(spot);
        double cosine = -(direction[0] * spot[0] + direction[1] * spot[1] + direction[2] * spot[2]);
        attenuation = !valid || cosine <= 0.0 ? 0.0 : pow(cosine, light->spot_exponent);
        if (light->limiting_cone) {
            // integrate the cone boundary across one pixel instead of leaving a jagged binary cutoff (§11.4).
            double width = distance > 0.0 ? .5 / (run->density * distance) *
                (fabs(spot[0] + cosine * direction[0]) / scale[0] + fabs(spot[1] + cosine * direction[1]) / scale[1]) : 0.0;
            attenuation *= width > 0.0 ? clamp_unit((float)(.5 + (cosine - light->cone_cosine) / (2.0 * width))) :
                cosine >= light->cone_cosine ? 1.0 : 0.0;
        }
    }
    bool specular = node->kind == RDT_SVG_FILTER_SPECULAR;
    if (specular) { direction[2] += 1.0; svg_filter_normalize(direction); }
    double dot = fmax(0.0, normal[0] * direction[0] + normal[1] * direction[1] + normal[2] * direction[2]);
    double intensity = light->constant * attenuation * (specular ? pow(dot, light->exponent) : dot);
    float values[4] = {};
    for (unsigned channel = 0; channel < 3; channel++) values[channel] = clamp_unit((float)(intensity * ((color >> (channel * 8)) & 255u) / 255.0));
    // specular RGB is already premultiplied; its maximum channel is the output alpha (§9.19).
    values[3] = specular ? fmaxf(values[0], fmaxf(values[1], values[2])) : 1.0f;
    return svg_filter_pack(values);
}

static bool svg_filter_evaluate_node(const RdtSvgFilterProgram* program, size_t index, SvgFilterExecution* execution) {
    const RdtSvgFilterNode* node = &program->nodes[index];
    SvgFilterImage* result = &execution->images[index];
    result->region = svg_filter_node_region(program, node, execution); result->linear = node->linear;
    if (!node->valid || !svg_filter_nonempty(svg_filter_intersect(execution->region, result->region)) ||
        (program->primitive_bbox && !svg_filter_nonempty(execution->run->geometry))) return true;
    float scale_x = svg_filter_primitive_scale(program, execution->run, 0), scale_y = svg_filter_primitive_scale(program, execution->run, 1);
    if (node->kind == RDT_SVG_FILTER_DISPLACEMENT || node->kind == RDT_SVG_FILTER_OFFSET || node->kind == RDT_SVG_FILTER_TILE) {
        const SvgFilterImage* source = svg_filter_image(execution, node->input);
        result->linear = source ? source->linear : false;
    } else if (node->kind == RDT_SVG_FILTER_FLOOD || node->kind == RDT_SVG_FILTER_IMAGE) result->linear = false;
    if (node->kind == RDT_SVG_FILTER_BLUR) result->surface = svg_filter_blur(execution, node->input,
        node->values[0] * scale_x, node->values[1] * scale_y, node->linear, node->variant);
    else if (node->kind == RDT_SVG_FILTER_MORPHOLOGY) result->surface = svg_filter_morphology(execution, node->input,
        node->values[0] * scale_x, node->values[1] * scale_y, node->variant != 0, node->linear);
    else result->surface = svg_filter_surface(execution);
    if (!result->surface) return false;
    if (node->kind == RDT_SVG_FILTER_IMAGE && execution->run->draw_image &&
        !execution->run->draw_image(execution->run->image_context, node, execution->run, result->region, execution->grid, result->surface)) return false;
    ImageSurface* shadow = node->kind == RDT_SVG_FILTER_SHADOW ? svg_filter_blur(execution, node->input,
        node->values[0] * scale_x, node->values[1] * scale_y, node->linear) : nullptr;
    if (node->kind == RDT_SVG_FILTER_SHADOW && !shadow) return false;
    uint32_t flood = render_pixel_pack_abgr(render_pixel_premultiply_channel(node->color.r, node->color.a),
        render_pixel_premultiply_channel(node->color.g, node->color.a), render_pixel_premultiply_channel(node->color.b, node->color.a), node->color.a);
    flood = svg_filter_convert(flood, false, result->linear);
    size_t pixels = (size_t)result->surface->width * (size_t)result->surface->height;
    SvgFilterNoiseFrame noise = node->kind == RDT_SVG_FILTER_TURBULENCE ? svg_filter_noise_frame(node, result->region, scale_x, scale_y,
        execution->run->geometry, program->primitive_bbox) : SvgFilterNoiseFrame{};
    size_t operations = node->kind == RDT_SVG_FILTER_MATRIX ? 25 : node->kind == RDT_SVG_FILTER_MERGE ? node->merge_count + 1 :
        node->kind == RDT_SVG_FILTER_TURBULENCE ? 4 * (size_t)node->values[2] + 1 :
        node->kind == RDT_SVG_FILTER_DIFFUSE || node->kind == RDT_SVG_FILTER_SPECULAR ? 80 :
        node->kind == RDT_SVG_FILTER_TILE ? 4 : 1;
    bool valid = operations && pixels <= SIZE_MAX / operations && svg_filter_spend(execution, pixels * operations);
    uint32_t* destination = (uint32_t*)result->surface->pixels;
    for (int row = 0; valid && row < result->surface->height; row++) for (int column = 0; valid && column < result->surface->width; column++) {
        float ux = ((float)column + execution->grid.left + .5f) / execution->run->density;
        float uy = ((float)row + execution->grid.top + .5f) / execution->run->density;
        size_t offset = (size_t)row * (size_t)result->surface->width + (size_t)column;
        if (ux < result->region.left || uy < result->region.top || ux >= result->region.right || uy >= result->region.bottom) { destination[offset] = 0; continue; }
        uint32_t first = svg_filter_sample(execution, node->input, (float)column, (float)row, node->linear), pixel = first;
        switch (node->kind) {
        case RDT_SVG_FILTER_BLUR: case RDT_SVG_FILTER_MORPHOLOGY: continue;
        case RDT_SVG_FILTER_FLOOD: pixel = flood; break;
        case RDT_SVG_FILTER_IMAGE: pixel = destination[offset]; break;
        case RDT_SVG_FILTER_DIFFUSE: case RDT_SVG_FILTER_SPECULAR:
            pixel = svg_filter_lighting(program, node, execution, (float)column, (float)row, flood); break;
        case RDT_SVG_FILTER_TURBULENCE: pixel = svg_filter_noise_pixel(node, noise,
            ux - (program->primitive_bbox ? execution->run->geometry.left : 0.0f),
            uy - (program->primitive_bbox ? execution->run->geometry.top : 0.0f)); break;
        case RDT_SVG_FILTER_TILE:
            pixel = svg_filter_sample_tile(execution, node->input, ux, uy, result->linear); break;
        case RDT_SVG_FILTER_DISPLACEMENT: {
            uint32_t map = svg_filter_sample(execution, node->input2, (float)column, (float)row, node->linear);
            uint8_t alpha = (uint8_t)(map >> 24); float channels[2];
            for (unsigned axis = 0; axis < 2; axis++) {
                unsigned selector = (node->variant >> (axis * 2)) & 3u;
                uint8_t channel = (uint8_t)(map >> (selector * 8));
                channels[axis] = (float)(selector == 3 ? channel : render_pixel_unpremultiply_channel(channel, alpha)) / 255.0f;
            }
            pixel = svg_filter_sample(execution, node->input,
                (float)column + node->values[0] * scale_x * execution->run->density * (channels[0] - .5f),
                (float)row + node->values[0] * scale_y * execution->run->density * (channels[1] - .5f), result->linear); break;
        }
        case RDT_SVG_FILTER_OFFSET: pixel = svg_filter_sample(execution, node->input,
            (float)column - node->values[0] * scale_x * execution->run->density,
            (float)row - node->values[1] * scale_y * execution->run->density, result->linear); break;
        case RDT_SVG_FILTER_MERGE:
            pixel = 0;
            // every graph image is premultiplied; a straight-destination helper would multiply alpha twice.
            for (size_t input = 0; input < node->merge_count; input++) pixel = render_pixel_source_over_premultiplied_pair(pixel,
                svg_filter_sample(execution, node->merge_inputs[input], (float)column, (float)row, node->linear));
            break;
        case RDT_SVG_FILTER_MATRIX: {
            float source[4], values[4]; svg_filter_unpack(first, source);
            for (unsigned channel = 0; channel < 3; channel++) source[channel] = source[3] > 0.0f ? source[channel] / source[3] : 0.0f;
            for (unsigned channel = 0; channel < 4; channel++) {
                values[channel] = node->values[channel * 5 + 4];
                for (unsigned component = 0; component < 4; component++) values[channel] += node->values[channel * 5 + component] * source[component];
                values[channel] = clamp_unit(values[channel]);
            }
            for (unsigned channel = 0; channel < 3; channel++) values[channel] *= values[3];
            pixel = svg_filter_pack(values); break;
        }
        case RDT_SVG_FILTER_SHADOW: {
            float px = (float)column - node->values[2] * scale_x * execution->run->density;
            float py = (float)row - node->values[3] * scale_y * execution->run->density;
            uint32_t alpha = px < 0 || py < 0 || px >= (float)shadow->width || py >= (float)shadow->height ? 0 :
                render_pixel_sample_bilinear((uint8_t*)shadow->pixels, shadow->width, shadow->height, shadow->pitch, px, py, false, true) >> 24;
            pixel = render_pixel_source_over_premultiplied_pair(render_pixel_scale_premultiplied(flood, (uint8_t)alpha), first); break;
        }
        case RDT_SVG_FILTER_COMPOSITE: pixel = svg_filter_composite(first,
            svg_filter_sample(execution, node->input2, (float)column, (float)row, node->linear), node->variant, node->values); break;
        case RDT_SVG_FILTER_BLEND: {
            uint32_t second = svg_filter_sample(execution, node->input2, (float)column, (float)row, node->linear);
            if (node->variant) {
                float colors[3], source[4], background[4], values[4];
                render_composite_blend_rgb(render_pixel_unpremultiply_abgr(second), render_pixel_unpremultiply_abgr(first), node->blend_mode, colors);
                svg_filter_unpack(first, source); svg_filter_unpack(second, background);
                values[3] = source[3];
                for (unsigned channel = 0; channel < 3; channel++) values[channel] =
                    (1.0f - background[3]) * source[channel] + background[3] * colors[channel] * source[3];
                pixel = svg_filter_pack(values); break;
            }
            uint32_t mixed = render_composite_blend_pixel(render_pixel_unpremultiply_abgr(second), render_pixel_unpremultiply_abgr(first), node->blend_mode);
            pixel = render_pixel_premultiply_abgr(mixed); break;
        }
        default: valid = false; break;
        }
        destination[offset] = pixel;
    }
    if (shadow) image_surface_destroy(shadow);
    return valid;
}

void render_svg_filter_resample_source(const RdtSvgFilterRun* run, const ImageSurface* source,
    Bound source_bounds, Bound grid, ImageSurface* output) {
    uint32_t* pixels = (uint32_t*)output->pixels;
    for (int row = 0; row < output->height; row++) for (int column = 0; column < output->width; column++) {
        float x = (grid.left + (float)column + .5f) / run->density;
        float y = (grid.top + (float)row + .5f) / run->density;
        if (!rdt_matrix_project_point(&run->frame, x, y, &x, &y)) {
            pixels[(size_t)row * (size_t)output->width + (size_t)column] = 0;
            continue;
        }
        x -= source_bounds.left + .5f; y -= source_bounds.top + .5f;
        pixels[(size_t)row * (size_t)output->width + (size_t)column] = !source || x < -.5f || y < -.5f ||
            x >= (float)source->width - .5f || y >= (float)source->height - .5f ? 0 :
            render_pixel_sample_bilinear((uint8_t*)source->pixels, source->width, source->height, source->pitch,
                x, y, false, true, source->alpha_mode == IMAGE_ALPHA_STRAIGHT);
    }
}

bool render_svg_filter_execute(const RdtSvgFilterProgram* program, const RdtSvgFilterRun* run,
    ImageSurface** result, Bound* bounds, RdtMatrix* placement) {
    if (result) *result = nullptr;
    if (!program || !run || !run->scratch || !result || !bounds || !placement || !program->valid ||
        !isfinite(run->density) || run->density <= 0.0f || !program->count || program->count > RDT_SVG_FILTER_MAX_NODES) return false;
    SvgFilterExecution execution = {};
    RdtSvgFilterRun budget = *run;
    if (!budget.work_used) budget.work_used = lam::up(&execution.work);
    run = &budget; execution.run = run;
    if (!render_svg_filter_region(program, run, &execution.region)) return false;
    execution.grid = {floorf(execution.region.left * run->density), floorf(execution.region.top * run->density),
        ceilf(execution.region.right * run->density), ceilf(execution.region.bottom * run->density)};
    if (!svg_filter_nonempty(execution.grid)) return false;
    size_t bytes = program->count * sizeof(SvgFilterImage) + (program->count + 4) * sizeof(size_t);
    if (!render_memory_allow_allocation(run->memory, bytes)) return false;
    ScratchMark mark = scratch_mark(run->scratch);
    execution.images = (SvgFilterImage*)scratch_calloc(run->scratch, program->count * sizeof(SvgFilterImage));
    execution.references = (size_t*)scratch_calloc(run->scratch, (program->count + 4) * sizeof(size_t));
    bool valid = execution.images && execution.references;
    if (valid) {
        execution.source_index = program->count;
        execution.references[program->count - 1]++;
        // only the final primitive's dependency tree contributes to the filter (Filter Effects §9.3).
        for (size_t index = program->count; index > 0; index--) if (execution.references[index - 1])
            svg_filter_visit_inputs(&program->nodes[index - 1], svg_filter_count_input, &execution);
    }
    static const int standard_inputs[] = {RDT_SVG_FILTER_SOURCE, RDT_SVG_FILTER_FILL, RDT_SVG_FILTER_STROKE, RDT_SVG_FILTER_BACKGROUND};
    for (size_t slot = 0; valid && slot < 4; slot++) {
        SvgFilterImage* image = &execution.standard[slot]; image->region = execution.region;
        if (!execution.references[program->count + slot]) continue;
        image->surface = svg_filter_surface(&execution);
        valid = image->surface && svg_filter_spend(&execution, (size_t)image->surface->width * (size_t)image->surface->height);
        if (valid && !slot) {
            if (run->draw_source) valid = run->draw_source(run->source_context, run, execution.grid, image->surface);
            else render_svg_filter_resample_source(run, run->source, run->source_bounds, execution.grid, image->surface);
        } else if (valid && run->draw_input) valid = run->draw_input(run->input_context, standard_inputs[slot], run, execution.grid, image->surface);
    }
    if (valid) {
        for (size_t index = 0; valid && index < program->count; index++) {
            if (!execution.references[index]) continue;
            valid = svg_filter_evaluate_node(program, index, &execution);
            // subregion metadata outlives its pixels; release each image after its last graph consumer.
            svg_filter_visit_inputs(&program->nodes[index], svg_filter_retire_input, &execution);
            if (!execution.references[index] && execution.images[index].surface) {
                image_surface_destroy(execution.images[index].surface); execution.images[index].surface = nullptr;
            }
        }
        SvgFilterImage* last = &execution.images[program->count - 1];
        // a disabled final primitive is a valid transparent result, rather than an execution failure.
        if (valid && !last->surface) last->surface = svg_filter_surface(&execution);
        if (valid && last->surface && last->linear)
            valid = svg_filter_spend(&execution, (size_t)last->surface->width * (size_t)last->surface->height);
        if (valid && last->surface) {
            uint32_t* output = (uint32_t*)last->surface->pixels;
            if (last->linear) for (size_t index = 0, count = (size_t)last->surface->width * (size_t)last->surface->height; index < count; index++)
                output[index] = svg_filter_convert(output[index], true, false);
            *result = last->surface; last->surface = nullptr; *bounds = execution.grid;
            RdtMatrix scale = rdt_matrix_scale(1.0f / run->density, 1.0f / run->density);
            *placement = rdt_matrix_multiply(&run->frame, &scale);
        } else valid = false;
    }
    if (execution.images) for (size_t index = 0; index < program->count; index++) if (execution.images[index].surface) image_surface_destroy(execution.images[index].surface);
    for (size_t slot = 0; slot < 4; slot++) if (execution.standard[slot].surface) image_surface_destroy(execution.standard[slot].surface);
    scratch_restore(run->scratch, mark);
    if (!valid) log_error("SVG_FILTER_EXECUTION: graph failed or exceeded the document memory/work budget");
    return valid;
}
