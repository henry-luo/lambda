#include "render_metrics.hpp"
// ==========================================================================
// DisplayList storage and lifecycle helpers.
// ==========================================================================

#include "render.hpp"
#include "rdt_path_internal.hpp"
#include "../lib/log.h"
#include "../lib/mem_factory.h"
#include "../lib/memtrack.h"
#include "../lib/math_checked.hpp"
#include <string.h>
#include <limits.h>
#include <math.h>
#include <new>
#include "../lib/generation.h"
#include "../lib/hashmap_typed.hpp"


static const DisplayOpDescriptor DISPLAY_OP_DESCRIPTORS[] = {
#define DL_OP_DESCRIPTOR(name, flags, owned_kind, owned_offset) \
    { name, flags, owned_kind, owned_offset },
    DISPLAY_OP_LIST(DL_OP_DESCRIPTOR)
#undef DL_OP_DESCRIPTOR
};
static_assert(sizeof(DISPLAY_OP_DESCRIPTORS) / sizeof(DISPLAY_OP_DESCRIPTORS[0]) == DL_OP_COUNT,
              "DisplayOp descriptor table must match DISPLAY_OP_LIST");

const DisplayOpDescriptor* dl_op_descriptor(DisplayOp op) {
    if (op < DL_FILL_RECT || op >= DL_OP_COUNT) return nullptr;
    return &DISPLAY_OP_DESCRIPTORS[(int)op];
}

bool dl_op_has_flags(DisplayOp op, uint32_t flags) {
    const DisplayOpDescriptor* desc = dl_op_descriptor(op);
    return desc && (desc->flags & flags) == flags;
}

bool dl_supports_premultiplied_capture(const DisplayList* list) {
    if (!list) return false;
    DisplayListCursor cursor(list);
    while (const DisplayItem* next = cursor.next()) {
        const DisplayItem& item = *next;
        if (!dl_op_has_flags(item.op, DL_OP_FLAG_PREMULTIPLIED_CAPTURE)) return false;
        // the color-emoji writer still forces opaque output; coverage glyphs preserve premultiplied alpha.
        if (item.op == DL_DRAW_GLYPH && item.draw_glyph.is_color_emoji) return false;
    }
    return !cursor.failed;
}

static uint64_t dl_fingerprint_fold(uint64_t hash, const void* data, size_t length) {
    return data && length ? hashmap_xxhash3(data, length, hash, 0x5bd1e995u) : hashmap_xxhash3(&hash, sizeof(hash), 0, 1);
}

static uint64_t dl_fingerprint_clip_shapes(uint64_t hash, const DlClipShapeStack* stack) {
    if (!stack) return dl_fingerprint_fold(hash, nullptr, 0);
    hash = dl_fingerprint_fold(hash, &stack->depth, sizeof(stack->depth));
    for (int d = 0; d < stack->depth && d < RDT_MAX_CLIP_SHAPES; d++) {
        hash = dl_fingerprint_fold(hash, &stack->type[d], sizeof(stack->type[d]));
        hash = dl_fingerprint_fold(hash, stack->params[d], sizeof(stack->params[d]));
        int count = stack->polygon_count[d];
        hash = dl_fingerprint_fold(hash, &count, sizeof(count));
        if (count > 0) {
            hash = dl_fingerprint_fold(hash, stack->polygon_vx[d].get(), (size_t)count * sizeof(float));
            hash = dl_fingerprint_fold(hash, stack->polygon_vy[d].get(), (size_t)count * sizeof(float));
        }
    }
    return hash;
}

// Payloads owned per recording are folded by content and their pointers cleared from the
// item copy; borrowed glyph-cache buffers and resource handles stay as identities.
bool dl_content_fingerprint(const DisplayList* list, uint64_t* fingerprint) {
    if (!list || !fingerprint) return false;
    ImageSurfaceReadScope read_scope;  // resolved image pixels stay valid while folded
    uint64_t hash = 0x9e3779b97f4a7c15ull;
    for (int index = 0; index < list->item_count(); index++) {
        const DisplayItem* source = &list->data()[index];
        alignas(DisplayItem) unsigned char bytes[sizeof(DisplayItem)];
        memcpy(bytes, source, sizeof(DisplayItem));
        auto clear = [&](size_t offset, size_t size) { memset(bytes + offset, 0, size); };
        switch (source->op) {
            case DL_DRAW_PICTURE: case DL_DRAW_FRAGMENT: case DL_VIDEO_PLACEHOLDER:
            case DL_WEBVIEW_LAYER_PLACEHOLDER: case DL_APPLY_FILTER:
                return false;
            case DL_FILL_PATH: case DL_STROKE_PATH: case DL_FILL_LINEAR_GRADIENT:
            case DL_FILL_RADIAL_GRADIENT: case DL_PUSH_CLIP: {
                // every path-carrying op starts with its path, at one union offset
                const RdtPath* path = source->fill_path.path.get();
                int entries = path ? path->count : 0;
                hash = dl_fingerprint_fold(hash, &entries, sizeof(entries));
                if (entries > 0) hash = dl_fingerprint_fold(hash, path->entries, (size_t)entries * sizeof(RdtPath::Entry));
                clear(offsetof(DisplayItem, fill_path.path), sizeof(void*));
                if (source->op == DL_STROKE_PATH) {
                    const DlStrokePath& stroke = source->stroke_path;
                    hash = dl_fingerprint_fold(hash, stroke.dash_array.get(), (size_t)max(stroke.dash_count, 0) * sizeof(float));
                    clear(offsetof(DisplayItem, stroke_path.dash_array), sizeof(void*));
                } else if (source->op == DL_FILL_LINEAR_GRADIENT || source->op == DL_FILL_RADIAL_GRADIENT) {
                    bool linear = source->op == DL_FILL_LINEAR_GRADIENT;
                    const RdtGradientStop* stops = linear ? source->fill_linear_gradient.stops.get()
                                                          : source->fill_radial_gradient.stops.get();
                    int count = linear ? source->fill_linear_gradient.stop_count : source->fill_radial_gradient.stop_count;
                    const RdtGradientOptions& options = linear ? source->fill_linear_gradient.options
                                                               : source->fill_radial_gradient.options;
                    hash = dl_fingerprint_fold(hash, stops, (size_t)max(count, 0) * sizeof(RdtGradientStop));
                    hash = dl_fingerprint_fold(hash, options.dash_array.get(), (size_t)max(options.dash_count, 0) * sizeof(float));
                    clear(linear ? offsetof(DisplayItem, fill_linear_gradient.stops) : offsetof(DisplayItem, fill_radial_gradient.stops), sizeof(void*));
                    clear(linear ? offsetof(DisplayItem, fill_linear_gradient.options.dash_array)
                                 : offsetof(DisplayItem, fill_radial_gradient.options.dash_array), sizeof(void*));
                }
                break;
            }
            case DL_DRAW_IMAGE: {
                // images fold by the pixels they draw: a frame-local buffer (a rasterised
                // gradient) or an owner recreated per recording (a rasterised text shadow)
                // keeps its fingerprint while its pixels do
                DlResolvedImage image = {};
                if (!dl_draw_image_resolve(&source->draw_image, &image) || !image.pixels ||
                    image.width <= 0 || image.height <= 0 || image.stride < image.width) return false;
                for (int row = 0; row < image.height; row++)
                    hash = dl_fingerprint_fold(hash, image.pixels.get() + (size_t)row * image.stride,
                        (size_t)image.width * sizeof(uint32_t));
                hash = dl_fingerprint_fold(hash, &image.straight_alpha, sizeof(image.straight_alpha));
                clear(offsetof(DisplayItem, draw_image.local_pixels), sizeof(void*));
                clear(offsetof(DisplayItem, draw_image.snapshot_lease), sizeof(void*));
                clear(offsetof(DisplayItem, draw_image.resource), sizeof(source->draw_image.resource));
                clear(offsetof(DisplayItem, draw_image.resource_generation), sizeof(source->draw_image.resource_generation));
                break;
            }
            case DL_BLIT_SURFACE_SCALED:
                if (source->blit_surface_scaled.local_source) return false;
                hash = dl_fingerprint_clip_shapes(hash, source->blit_surface_scaled.clip_shapes.get());
                clear(offsetof(DisplayItem, blit_surface_scaled.snapshot_lease), sizeof(void*));
                clear(offsetof(DisplayItem, blit_surface_scaled.clip_shapes), sizeof(void*));
                break;
            case DL_FILL_SURFACE_RECT:
                hash = dl_fingerprint_clip_shapes(hash, source->fill_surface_rect.clip_shapes.get());
                clear(offsetof(DisplayItem, fill_surface_rect.clip_shapes), sizeof(void*));
                break;
            case DL_BEGIN_ELEMENT: case DL_END_ELEMENT:
                // accessibility semantics do not paint
                clear(offsetof(DisplayItem, element_marker.semantics), sizeof(RenderSemanticGroup));
                break;
            default:
                break;
        }
        hash = dl_fingerprint_fold(hash, bytes, sizeof(bytes));
    }
    *fingerprint = hash;
    return true;
}

bool dl_supports_vector_translation(const DisplayList* list, int first, int end) {
    if (!list || first < 0 || first > list->item_count() || end < -1 ||
        (end >= 0 && (end < first || end > list->item_count()))) return false;
    DisplayListCursor cursor(list, first, end);
    while (const DisplayItem* item = cursor.next())
        if (!dl_op_has_flags(item->op, DL_OP_FLAG_VECTOR_TRANSLATION)) return false;
    return !cursor.failed;
}

DisplayItem* dl_alloc_item(DisplayList* dl) {
    if (!dl) return nullptr;
    if (dl->capacity() == 0 && !dl->reserve(DL_INITIAL_CAPACITY)) return nullptr;
    DisplayItem item = {};
    if (!dl->append(item)) return nullptr;
    return &dl->back();
}

DisplayListCursor::DisplayListCursor(const DisplayList* list, int first, int end)
    : root(list), next_root(max(first, 0)), end_root(list ? list->item_count() : 0) {
    if (end >= 0) end_root = min(end_root, end);
    failed = list && (list->size() > list->capacity() || (list->size() && !list->data()));
}

DisplayListCursor::DisplayListCursor(const DisplayPayloadOwner* chunk)
    : root(nullptr), next_root(0), end_root(0) {
    push(chunk);
}

static bool dl_translate_point(float* x, float* y, float dx, float dy) {
    *x += dx; *y += dy;
    return isfinite(*x) && isfinite(*y);
}

static bool dl_translate_transform(RdtMatrix* matrix, bool* has_transform, float dx, float dy) {
    RdtMatrix offset = rdt_matrix_translate(dx, dy);
    *matrix = *has_transform ? rdt_matrix_multiply(&offset, matrix) : offset;
    *has_transform = true;
    const float values[] = {matrix->e11, matrix->e12, matrix->e13, matrix->e21, matrix->e22,
        matrix->e23, matrix->e31, matrix->e32, matrix->e33};
    for (float value : values) if (!isfinite(value)) return false;
    return true;
}

template<typename T>
static size_t dl_copy_placed_payload(T& payload, const T& original) {
    // start the active union member's lifetime; resource annotations copy borrowed handles, not leases.
    new (&payload) T(original);
    return sizeof(T);
}

const DisplayItem* DisplayListCursor::placed_item(const DisplayItem* source) {
    if (translate_x == 0 && translate_y == 0) return source;
    if (!dl_op_has_flags(source->op, DL_OP_FLAG_VECTOR_TRANSLATION)) { failed = true; return nullptr; }
    // only the active union member is defined; small commands need no copy of the largest payload.
    translated.op = source->op;
    memcpy(translated.bounds, source->bounds, sizeof(source->bounds));
    size_t copied_bytes = sizeof(source->op) + sizeof(source->bounds);
    bool valid = dl_translate_point(&translated.bounds[0], &translated.bounds[1], translate_x, translate_y) &&
        isfinite(translated.bounds[2]) && isfinite(translated.bounds[3]) &&
        isfinite(translated.bounds[0] + translated.bounds[2]) &&
        isfinite(translated.bounds[1] + translated.bounds[3]);
    auto transform = [&](auto& payload, const auto& original) {
        copied_bytes += dl_copy_placed_payload(payload, original);
        return dl_translate_transform(&payload.transform, &payload.has_transform, translate_x, translate_y);
    };
    auto rectangle = [&](auto& payload, const auto& original) {
        copied_bytes += dl_copy_placed_payload(payload, original);
        return dl_translate_point(&payload.x, &payload.y, translate_x, translate_y);
    };
    if (valid) switch (translated.op) {
        case DL_FILL_RECT:
            valid = rectangle(translated.fill_rect, source->fill_rect);
            break;
        case DL_FILL_ROUNDED_RECT:
            valid = rectangle(translated.fill_rounded_rect, source->fill_rounded_rect);
            break;
        case DL_FILL_PATH: valid = transform(translated.fill_path, source->fill_path); break;
        case DL_STROKE_PATH: valid = transform(translated.stroke_path, source->stroke_path); break;
        case DL_FILL_LINEAR_GRADIENT: valid = transform(translated.fill_linear_gradient, source->fill_linear_gradient); break;
        case DL_FILL_RADIAL_GRADIENT: valid = transform(translated.fill_radial_gradient, source->fill_radial_gradient); break;
        case DL_DRAW_IMAGE: valid = transform(translated.draw_image, source->draw_image); break;
        case DL_DRAW_PICTURE: valid = transform(translated.draw_picture, source->draw_picture); break;
        case DL_PUSH_CLIP: valid = transform(translated.push_clip, source->push_clip); break;
        case DL_BEGIN_ELEMENT:
        case DL_END_ELEMENT:
            copied_bytes += dl_copy_placed_payload(translated.element_marker, source->element_marker);
            valid = dl_translate_point(&translated.element_marker.marker_x,
                &translated.element_marker.marker_y, translate_x, translate_y);
            break;
        case DL_POP_CLIP: break;
        default: valid = false; break;
    }
    RENDER_METRIC_ADD(placement_command_copy_bytes, copied_bytes);
    if (!valid) { failed = true; return nullptr; }
    return &translated;
}

bool DisplayListCursor::push(const DisplayPayloadOwner* chunk, float edge_x, float edge_y) {
    if (!chunk || !chunk->commands || !chunk->command_spans || !chunk->span_count ||
        chunk->command_count <= 0 || depth >= DL_VALIDATE_ELEMENT_STACK_LIMIT ||
        chunk->commands->size() > chunk->commands->capacity() || !chunk->commands->data()) {
        failed = true; return false;
    }
    for (int i = 0; i < depth; i++) if (chunks[i].owner == chunk) {
        failed = true; return false;
    }
    float dx = chunk->translate_x + edge_x + (depth ? chunks[depth - 1].translate_x : 0);
    float dy = chunk->translate_y + edge_y + (depth ? chunks[depth - 1].translate_y : 0);
    if (!isfinite(dx) || !isfinite(dy)) { failed = true; return false; }
    chunks[depth++] = {chunk, 0, 0, 0, dx, dy};
    return true;
}

const DisplayItem* DisplayListCursor::next() {
    item = nullptr;
    while (!failed) {
        if (!depth) {
            if (!root || next_root >= end_root) return nullptr;
            source_index = index = root_index = next_root++;
            owner = nullptr;
            translate_x = translate_y = 0;
            item = &root->data()[index];
            if (item->op == DL_DRAW_FRAGMENT) {
                if (!push(item->draw_fragment.owner)) break;
                continue;
            }
            return item;
        }
        ChunkFrame& frame = chunks[depth - 1];
        owner = frame.owner;
        translate_x = frame.translate_x; translate_y = frame.translate_y;
        if (frame.span == owner->span_count) {
            if (frame.logical != owner->command_count) { failed = true; break; }
            depth--; continue;
        }
        const DisplayCommandSpan& span = owner->command_spans[frame.span];
        if (span.child) {
            if (span.child->command_count <= 0 || span.child->command_count > INT_MAX - frame.logical) {
                failed = true; break;
            }
            frame.span++; frame.logical += span.child->command_count;
            if (!push(span.child, span.translate_x, span.translate_y)) break;
            continue;
        }
        if (span.translate_x != 0 || span.translate_y != 0 ||
            span.start < 0 || span.count <= 0 || span.start > owner->commands->item_count() ||
            span.count > owner->commands->item_count() - span.start) { failed = true; break; }
        if (frame.offset == span.count) { frame.offset = 0; frame.span++; continue; }
        if (frame.logical >= owner->command_count) { failed = true; break; }
        source_index = span.start + frame.offset++;
        item = &owner->commands->data()[source_index];
        index = frame.logical++;
        // immutable chunks express nesting through spans, never through mutable frame-reference commands.
        if (item->op == DL_DRAW_FRAGMENT) { failed = true; break; }
        item = placed_item(item);
        return item;
    }
    item = nullptr;
    return nullptr;
}

int DisplayListCursor::scope_size() const {
    return owner ? owner->command_count : (root ? root->item_count() : 0);
}

void DisplayListCursor::skip_element() {
    if (!item || item->op != DL_BEGIN_ELEMENT || item->element_marker.matching_index <= index) return;
    int match = item->element_marker.matching_index;
    if (match >= scope_size()) { failed = true; return; }
    if (!owner) { next_root = match + 1; return; }
    if (index == 0 && match == owner->command_count - 1) { depth--; return; }
    const DisplayPayloadOwner* scope = owner;
    while (next()) if (owner == scope && index == match) return;
    failed = true;
}

bool dl_reference_fragment(DisplayList* list, DisplayPayloadOwner* owner, Bound bounds) {
    if (!list || !owner || !owner->destroy || !owner->command_spans || !owner->span_count ||
        !ref_count_retain(&owner->refs)) return false;
    DisplayItem* item = dl_alloc_item(list);
    if (!item) { dl_payload_owner_release(owner); return false; }
    item->op = DL_DRAW_FRAGMENT; item->draw_fragment.owner = lam::counted(owner);
    item->bounds[0] = bounds.left; item->bounds[1] = bounds.top;
    item->bounds[2] = bounds.right - bounds.left; item->bounds[3] = bounds.bottom - bounds.top;
    RENDER_METRIC_ADD(fragment_references, 1);
    RENDER_METRIC_ADD(fragment_commands, owner->command_count);
    return true;
}

// list indexes pin immutable value buffers; copied chunks acquire the same buffers.
struct DlSharedArray {
    RefCount refs;
    uint64_t hash;
    size_t bytes;
    unsigned kind;
    const void* data;
};
struct DlSharedArrayEntry { DlSharedArray* value; uint64_t serial = 0; }; // zero marks idle cached values
struct DlSharedArrayKeyOps {
    static uint64_t hash(const DlSharedArrayEntry& entry, uint64_t first, uint64_t second) {
        return hashmap_hash_bytes(&entry.value->hash, sizeof(entry.value->hash), first, second);
    }
    static bool equals(const DlSharedArrayEntry& a, const DlSharedArrayEntry& b) {
        return a.value->kind == b.value->kind && a.value->bytes == b.value->bytes &&
            memcmp(a.value->data, b.value->data, a.value->bytes) == 0;
    }
};
static void dl_shared_array_release(DlSharedArrayEntry* entry) {
    if (ref_count_release(&entry->value->refs) == REF_COUNT_LAST) mem_free(entry->value);
}
struct DlSharedArrayCache {
    TypedHashMap<DlSharedArrayEntry, DlSharedArrayKeyOps, dl_shared_array_release> index;
    size_t bytes;
    uint64_t next_serial;
};

size_t dl_shared_array_bytes(const DisplayList* list) {
    const DlSharedArrayCache* cache = list ? list->shared_arrays.get() : nullptr;
    if (!cache) return 0;
    const HashMap* map = cache->index.raw();
    return cache->bytes + sizeof(*cache) + sizeof(*map) + (map->nbuckets + 2) * map->bucketsz;
}

size_t dl_shared_array_reuse_bytes(const DisplayList* list) {
    if (!list || !list->shared_array_reuse_budget || !list->shared_arrays ||
        !list->shared_arrays->index.count()) return 0;
    size_t bytes = dl_shared_array_bytes(list);
    return bytes <= list->shared_array_reuse_budget ? bytes : 0;
}

static void dl_shared_array_cache_destroy(lam::Own<DlSharedArrayCache>& cache) {
    if (!cache) return;
    cache->index.destroy(); lam::free_owned(cache);
}

uint64_t dl_shared_array_checkpoint(const DisplayList* list) {
    return list && list->shared_arrays ? list->shared_arrays->next_serial : 0;
}

void dl_shared_array_rewind(DisplayList* list, uint64_t checkpoint) {
    DlSharedArrayCache* cache = list ? list->shared_arrays.get() : nullptr;
    if (!cache) return;
    size_t cursor = 0; DlSharedArrayEntry* entry;
    while (cache->index.next(&cursor, &entry)) {
        if (entry->serial < checkpoint) continue;
        DlSharedArrayEntry removed = *entry;
        cache->index.erase(removed);
        cache->bytes -= sizeof(DlSharedArray) + removed.value->bytes;
        dl_shared_array_release(&removed);
        cursor = 0; // erasure relocates buckets; overlay additions are bounded to one frame.
    }
    cache->next_serial = checkpoint ? checkpoint : 1;
}

template<typename T>
static const T* dl_share_array(DisplayList* dst, const DisplayList* source, const T* data, int count, unsigned kind) {
    if (!dst || !data || count <= 0) return nullptr;
    size_t bytes, allocation;
    if (!math_checked_mul((size_t)count, sizeof(T), &bytes) ||
        !math_checked_add(sizeof(DlSharedArray), bytes, &allocation)) return nullptr;
    if (!dst->shared_arrays) {
        DlSharedArrayCache* cache = (DlSharedArrayCache*)mem_calloc(1, sizeof(DlSharedArrayCache), MEM_CAT_RENDER);
        if (!cache) return nullptr;
        if (!cache->index.init(8)) { mem_free(cache); return nullptr; }
        cache->next_serial = 1;
        dst->shared_arrays = lam::own(cache);
    }
    DlSharedArray query = {}; query.bytes = bytes; query.kind = kind; query.data = data;
    query.hash = hashmap_hash_bytes(data, bytes, kind, 0);
    DlSharedArrayCache* cache = dst->shared_arrays;
    if (cache->next_serial == UINT64_MAX) return nullptr;
    DlSharedArrayEntry* existing = cache->index.get({&query});
    if (existing) {
        if (!existing->serial) {
            // reactivation belongs after the overlay checkpoint; rollback can
            // retire its lease only when no earlier command borrowed the value.
            existing->serial = cache->next_serial++;
            RENDER_METRIC_ADD(shared_array_reuses, 1);
        }
        RENDER_METRIC_ADD(shared_array_hits, 1);
        return (T*)existing->value->data;
    }
    const DlSharedArrayEntry* from = source && source->shared_arrays ? source->shared_arrays->index.get({&query}) : nullptr;
    DlSharedArray* value;
    if (from) {
        value = from->value;
        if (!ref_count_retain(&value->refs)) return nullptr;
        RENDER_METRIC_ADD(shared_array_retains, 1);
    } else {
        value = (DlSharedArray*)mem_alloc(allocation, MEM_CAT_RENDER);
        if (!value) return nullptr;
        *value = query; ref_count_init(&value->refs); value->data = value + 1;
        memcpy((void*)value->data, data, bytes);
        RENDER_METRIC_ADD(payload_copy_bytes, bytes);
    }
    cache->index.set({value, cache->next_serial});
    if (cache->index.oom()) { DlSharedArrayEntry entry = {value}; dl_shared_array_release(&entry); return nullptr; }
    cache->bytes += allocation;
    cache->next_serial++;
    return (T*)value->data;
}

const RdtGradientStop* dl_copy_stops(DisplayList* dl, const RdtGradientStop* stops, int count) {
    return dl_share_array(dl, nullptr, stops, count, 1);
}

const float* dl_copy_dashes(DisplayList* dl, const float* dashes, int count) {
    return dl_share_array(dl, nullptr, dashes, count, 2);
}

bool dl_copy_semantic_group(DisplayList* dl, RenderSemanticGroup* out,
                            const RenderSemanticGroup* source) {
    if (!dl || !out || !source || source->attribute_count < 0 ||
        (source->attribute_count && !source->attributes)) return false;
    RenderSemanticGroup copy = {};
    auto copy_string = [&](const char* value) -> const char* {
        if (!value) return nullptr;
        size_t bytes = strlen(value) + 1;
        char* result = (char*)scratch_alloc(&dl->arena, bytes);
        if (result) memcpy(result, value, bytes);
        return result;
    };
    if (source->attribute_count) {
        RenderSemanticAttribute* attrs = (RenderSemanticAttribute*)scratch_calloc(
            &dl->arena, (size_t)source->attribute_count * sizeof(RenderSemanticAttribute));
        if (!attrs) return false;
        for (int i = 0; i < source->attribute_count; i++) {
            attrs[i] = {lam::up(copy_string(source->attributes[i].name)), lam::up(copy_string(source->attributes[i].value))};
            if (!attrs[i].name || !attrs[i].value) return false;
        }
        copy.attributes = lam::up(attrs);
        copy.attribute_count = source->attribute_count;
    }
    copy.title = lam::up(copy_string(source->title));
    if (source->title && !copy.title) return false;
    *out = copy;
    return true;
}

bool dl_store_clip_shapes(DisplayList* dl, lam::Up<const DlClipShapeStack>* out,
                          ClipShape** clip_shapes, int clip_depth) {
    if (!dl || !out) return false;
    *out = nullptr;
    if (!clip_shapes || clip_depth <= 0) return true;
    if (clip_depth > RDT_MAX_CLIP_SHAPES) {
        log_warn("[RAD_CAP_DL_CLIP_SHAPES] truncating clip stack from %d to %d shapes",
                 clip_depth, RDT_MAX_CLIP_SHAPES);
        clip_depth = RDT_MAX_CLIP_SHAPES;
    }
    DlClipShapeStack* dst = (DlClipShapeStack*)scratch_calloc(&dl->arena, sizeof(DlClipShapeStack));
    if (!dst) return false;
    dst->depth = clip_depth;
    for (int i = 0; i < clip_depth; i++) {
        ClipShape* shape = clip_shapes[i];
        clip_shape_to_params(shape, &dst->type[i], dst->params[i]);
        if (shape && shape->type == CLIP_SHAPE_POLYGON &&
            shape->polygon.count > 0 && shape->polygon.vx && shape->polygon.vy) {
            size_t sz = (size_t)shape->polygon.count * sizeof(float);
            float* vx = (float*)scratch_alloc(&dl->arena, sz);
            float* vy = (float*)scratch_alloc(&dl->arena, sz);
            if (!vx || !vy) return false;
            memcpy(vx, shape->polygon.vx, sz);
            memcpy(vy, shape->polygon.vy, sz);
            dst->polygon_count[i] = shape->polygon.count;
            dst->polygon_vx[i] = lam::own_arr(vx);
            dst->polygon_vy[i] = lam::own_arr(vy);
        }
    }
    // publish only complete stacks; an allocation failure must never discard a clip.
    *out = lam::up(dst);
    return true;
}

int dl_restore_clip_shapes(const DlClipShapeStack* src, ClipShape* shapes,
                           ClipShape** shape_ptrs, ScratchArena* scratch,
                           float offset_x, float offset_y) {
    if (!src || !shapes || !shape_ptrs || src->depth <= 0) return 0;
    int depth = src->depth;
    if (depth > RDT_MAX_CLIP_SHAPES) {
        log_warn("[RAD_CAP_DL_CLIP_RESTORE] truncating stored clip stack from %d to %d shapes",
                 depth, RDT_MAX_CLIP_SHAPES);
        depth = RDT_MAX_CLIP_SHAPES;
    }
    int out_depth = 0;
    for (int i = 0; i < depth; i++) {
        if (src->type[i] == CLIP_SHAPE_NONE) continue;
        shapes[out_depth] = clip_shape_from_params(src->type[i], src->params[i]);
        if (src->type[i] == CLIP_SHAPE_POLYGON) {
            int count = src->polygon_count[i];
            if (count < 3 || !src->polygon_vx[i] || !src->polygon_vy[i]) continue;
            if ((offset_x != 0.0f || offset_y != 0.0f) && !scratch) continue;
            float* vx = src->polygon_vx[i];
            float* vy = src->polygon_vy[i];
            if (scratch) {
                vx = (float*)scratch_alloc(scratch, count * sizeof(float));
                vy = (float*)scratch_alloc(scratch, count * sizeof(float));
                if (!vx || !vy) continue;
                for (int pi = 0; pi < count; pi++) {
                    vx[pi] = src->polygon_vx[i][pi] - (shapes[out_depth].transformed ? 0.0f : offset_x);
                    vy[pi] = src->polygon_vy[i][pi] - (shapes[out_depth].transformed ? 0.0f : offset_y);
                }
            }
            shapes[out_depth].type = CLIP_SHAPE_POLYGON;
            shapes[out_depth].polygon = {lam::own_arr(vx), lam::own_arr(vy), count};
        }
        clip_shape_offset(&shapes[out_depth], offset_x, offset_y);
        shape_ptrs[out_depth] = &shapes[out_depth];
        out_depth++;
    }
    return out_depth;
}

static void dl_init_scratch(DisplayList* list, Arena* backing_arena) {
    mem_scratch_init(NULL, &list->arena, backing_arena, MEM_ROLE_RENDER, "display_list.scratch");
}

void DisplayList::init(Arena* backing_arena) {
    destroy();
    dl_init_scratch(this, backing_arena);
}

void dl_item_free_owned_payload(DisplayItem* item) {
    if (!item) return;
    if (item->op == DL_DRAW_IMAGE && item->draw_image.snapshot_lease) {
        image_surface_snapshot_release(item->draw_image.snapshot_lease);item->draw_image.snapshot_lease=nullptr;
    }
    if (item->op == DL_BLIT_SURFACE_SCALED && item->blit_surface_scaled.snapshot_lease) {
        image_surface_snapshot_release(item->blit_surface_scaled.snapshot_lease);
        item->blit_surface_scaled.snapshot_lease = nullptr;
    }
    const DisplayOpDescriptor* descriptor = dl_op_descriptor(item->op);
    if (!descriptor || descriptor->owned_payload_kind == DL_OWNED_PAYLOAD_NONE) return;
    // Ownership kind and offset live in the op row so new owned payloads cannot omit cleanup.
    void* field = (char*)item + descriptor->owned_payload_offset;
    if (descriptor->owned_payload_kind == DL_OWNED_PAYLOAD_PATH) {
        RdtPath** path = (RdtPath**)field;
        if (*path) { rdt_path_free(*path); *path = nullptr; }
    } else if (descriptor->owned_payload_kind == DL_OWNED_PAYLOAD_PICTURE) {
        RdtPicture** picture = (RdtPicture**)field;
        if (*picture) { rdt_picture_free(*picture); *picture = nullptr; }
    } else if (descriptor->owned_payload_kind == DL_OWNED_PAYLOAD_FRAGMENT) {
        DisplayPayloadOwner** owner = (DisplayPayloadOwner**)field;
        dl_payload_owner_release(*owner); *owner = nullptr;
    }
}

void DisplayList::clear() {
    DisplayList* dl = this;

    for (size_t i = 0; i < dl->size(); i++) {
        dl_item_free_owned_payload(&dl->data()[i]);
    }
    lam::ArrayList<DisplayItem>::clear();
    if (dl_shared_array_reuse_bytes(this) && memtrack_get_pressure_level() < MEM_PRESSURE_HIGH) {
        // the map keeps the leases while commands retire; independent copies
        // already pin their buffers. Idle values remain bounded by the full index charge.
        size_t cursor = 0; DlSharedArrayEntry* entry;
        while (shared_arrays->index.next(&cursor, &entry)) entry->serial = 0;
        shared_arrays->next_serial = 1;
    } else dl_shared_array_cache_destroy(shared_arrays);
    Arena* backing_arena = dl->arena.arena;
    scratch_release(&dl->arena);
    if (backing_arena) {
        // reusable display lists release scratch nodes on clear; re-register so
        // retained-fragment captures keep mem-dump accounting current.
        dl_init_scratch(dl, backing_arena);
    }
}

bool dl_trim_empty_arena(DisplayList* list) {
    if (!list) return false;
    ScratchArena* scratch = &list->arena;
    Arena* backing = scratch->arena;
    if (!backing || !scratch->scope_active || scratch->base.total_used || scratch->head ||
        scratch->open_scope || arena_active_scope_count(backing) != 1 ||
        arena_total_used(backing) || arena_chunk_count(backing) <= 1) return false;
    // a rewound capture can leave megabytes of idle capacity; no live payload or earlier arena data may be reset.
    scratch_release(scratch);
    arena_clear(backing);
    dl_init_scratch(list, backing);
    return true;
}

void DisplayList::destroy() {
    clear();
    dl_shared_array_cache_destroy(shared_arrays);
    shared_array_reuse_budget = 0;
    lam::ArrayList<DisplayItem>::release();
    scratch_release(&arena);
}

int DisplayList::item_count() const {
    if (size() > (size_t)INT_MAX) {
        log_error("display_list_item_count_overflow: count=%zu", size());
        return INT_MAX;
    }
    return (int)size(); // INT_CAST_OK: DisplayList public count API is int.
}

static bool dl_item_contains_glyphs(const DisplayItem* item) {
    // native text duplicates re-shape through a shared font cache, so they also require serial replay.
    return item->op == DL_DRAW_GLYPH ||
        (item->op == DL_DRAW_PICTURE && rdt_picture_is_text(item->draw_picture.picture));
}

bool DisplayList::contains_glyphs() const {
    DisplayListCursor cursor(this);
    while (const DisplayItem* next = cursor.next()) {
        RENDER_METRIC_ADD(glyph_command_checks, 1);
        if (dl_item_contains_glyphs(next)) return true;
    }
    return false;
}

void dl_init(DisplayList* dl, Arena* backing_arena) {
    if (dl) dl->init(backing_arena);
}

void dl_clear(DisplayList* dl) {
    if (dl) dl->clear();
}

void dl_destroy(DisplayList* dl) {
    if (dl) dl->destroy();
}

int dl_item_count(const DisplayList* dl) {
    return dl ? dl->item_count() : 0;
}

bool dl_contains_glyphs(const DisplayList* dl) {
    return dl ? dl->contains_glyphs() : false;
}

static void dl_validation_set(DisplayListValidationResult* result, bool valid,
                              int index, const char* message, int clip_depth,
                              int backdrop_depth, int shadow_clip_depth,
                              int element_depth) {
    if (!result) return;
    result->valid = valid;
    result->first_error_index = index;
    result->message = lam::up(message);
    result->clip_depth = clip_depth;
    result->backdrop_depth = backdrop_depth;
    result->shadow_clip_depth = shadow_clip_depth;
    result->element_depth = element_depth;
}

typedef struct {
    int clip_depth;
    int backdrop_depth;
    int shadow_clip_depth;
    int element_depth;
} DisplayListValidationStack;

static bool dl_validation_fail(DisplayListValidationResult* result, int index,
                               const char* message,
                               const DisplayListValidationStack* stack) {
    DisplayListValidationStack empty = {};
    const DisplayListValidationStack* s = stack ? stack : &empty;
    dl_validation_set(result, false, index, message, s->clip_depth, s->backdrop_depth,
                      s->shadow_clip_depth, s->element_depth);
    return false;
}

static bool dl_validate_positive_image(const DlDrawImage* image) {
    return (image->local_pixels || !image->resource.is_null()) && image->src_w > 0 && image->src_h > 0 &&
           image->src_stride > 0 && image->dst_w >= 0.0f && image->dst_h >= 0.0f;
}

static bool dl_has_negative_size(float w, float h) {
    return w < 0.0f || h < 0.0f;
}

static bool dl_validate_resource_size(const void* resource, float w, float h) {
    return resource && !dl_has_negative_size(w, h);
}

static bool dl_retainable_generation_resource(const void* resource, uint64_t generation) {
    return !resource || generation_stamped(generation);
}

// Retained items reference surfaces by handle; an unregistered surface (null
// handle) cannot be checked for liveness later, so it is never retained.
static bool dl_retainable_surface(const void* borrowed, lam::Handle<ImageSurface> owner,
                                  uint64_t generation) {
    return !borrowed || (!owner.is_null() && generation_stamped(generation));
}

static bool dl_validate_stream(DisplayListCursor& cursor, int end,
    DisplayListValidationResult* result, DisplayChunkValidation* validation) {
    dl_validation_set(result, true, -1, "ok", 0, 0, 0, 0);
    DisplayListValidationStack stack = {};
    DisplayChunkValidation observed = {};
    struct ElementScope { const DisplayPayloadOwner* owner; int begin, end; };
    ElementScope element_stack[DL_VALIDATE_ELEMENT_STACK_LIMIT];
    auto fail_at = [&](int index, const char* message) -> bool {
        return dl_validation_fail(result, index, message, &stack);
    };

    while (const DisplayItem* item = cursor.next()) {
        int i = cursor.index;
        auto fail = [&](const char* message) -> bool { return fail_at(i, message); };
        const DisplayChunkValidation* chunk = retained_dl_cursor_validation(cursor);
        if (chunk) {
            RENDER_METRIC_ADD(validation_chunk_checks, 1);
            // balanced immutable chunks preserve caller state, but their peaks still consume nesting space.
            if (chunk->max_element_depth > DL_VALIDATE_ELEMENT_STACK_LIMIT - stack.element_depth)
                return fail("element marker nesting is too deep");
            if (chunk->max_fragment_depth > DL_VALIDATE_ELEMENT_STACK_LIMIT - cursor.depth + 1)
                return fail_at(cursor.root_index, "fragment graph is invalid or cyclic");
            if (validation) {
                observed.properties.has_glyphs |= chunk->properties.has_glyphs;
                observed.properties.has_video |= chunk->properties.has_video;
                observed.max_element_depth = max(observed.max_element_depth,
                    stack.element_depth + chunk->max_element_depth);
                observed.max_fragment_depth = max(observed.max_fragment_depth,
                    cursor.depth - 1 + chunk->max_fragment_depth);
            }
            cursor.skip_element();
            continue;
        }
        RENDER_METRIC_ADD(validation_command_checks, 1);
        if (item->op < DL_FILL_RECT || item->op >= DL_OP_COUNT) {
            return fail("unknown display op");
        }

        switch (item->op) {
            case DL_DRAW_FRAGMENT:
                return fail("fragment reference was not expanded");
            case DL_FILL_RECT:
                if (dl_has_negative_size(item->fill_rect.w, item->fill_rect.h)) {
                    return fail("fill rect has negative size");
                }
                break;
            case DL_FILL_ROUNDED_RECT:
                if (dl_has_negative_size(item->fill_rounded_rect.w, item->fill_rounded_rect.h) ||
                    dl_has_negative_size(item->fill_rounded_rect.rx, item->fill_rounded_rect.ry)) {
                    return fail("rounded rect has negative size");
                }
                break;
            case DL_FILL_PATH:
                if (!item->fill_path.path) {
                    return fail("fill path is null");
                }
                break;
            case DL_STROKE_PATH:
                if (!item->stroke_path.path || item->stroke_path.width < 0.0f ||
                    item->stroke_path.dash_count < 0 ||
                    (item->stroke_path.dash_count > 0 && !item->stroke_path.dash_array)) {
                    return fail("stroke path payload is invalid");
                }
                break;
            case DL_FILL_LINEAR_GRADIENT:
                if (!item->fill_linear_gradient.path || !item->fill_linear_gradient.stops ||
                    item->fill_linear_gradient.stop_count <= 0 ||
                    !rdt_gradient_options_valid(&item->fill_linear_gradient.options)) {
                    return fail("linear gradient payload is invalid");
                }
                break;
            case DL_FILL_RADIAL_GRADIENT:
                if (!item->fill_radial_gradient.path || !item->fill_radial_gradient.stops ||
                    item->fill_radial_gradient.stop_count <= 0 ||
                    !rdt_gradient_options_valid(&item->fill_radial_gradient.options) ||
                    item->fill_radial_gradient.r < 0.0f) {
                    return fail("radial gradient payload is invalid");
                }
                break;
            case DL_DRAW_IMAGE:
                if (!dl_validate_positive_image(&item->draw_image)) {
                    return fail("draw image payload is invalid");
                }
                break;
            case DL_DRAW_GLYPH:
                if (!render_glyph_bitmap_has_ink(&item->draw_glyph.bitmap)) {
                    return fail("glyph payload is invalid");
                }
                break;
            case DL_DRAW_PICTURE:
                if (!item->draw_picture.picture) {
                    return fail("picture payload is invalid");
                }
                break;
            case DL_PUSH_CLIP:
                if (!item->push_clip.path) {
                    return fail("clip path is null");
                }
                stack.clip_depth++;
                break;
            case DL_POP_CLIP:
                if (stack.clip_depth <= 0) {
                    return fail("clip pop without matching push");
                }
                stack.clip_depth--;
                break;
            case DL_FILL_SURFACE_RECT:
                if (dl_has_negative_size(item->fill_surface_rect.w, item->fill_surface_rect.h)) {
                    return fail("surface fill has negative size");
                }
                break;
            case DL_BLIT_SURFACE_SCALED:
                if ((!item->blit_surface_scaled.local_source && item->blit_surface_scaled.src_resource.is_null()) ||
                    dl_has_negative_size(item->blit_surface_scaled.dst_w, item->blit_surface_scaled.dst_h)) {
                    return fail("surface blit payload is invalid");
                }
                break;
            case DL_COMPOSITE_OPACITY:
                if (stack.backdrop_depth <= 0) {
                    return fail("opacity composite without saved backdrop");
                }
                if (dl_has_negative_size(item->composite_opacity.w, item->composite_opacity.h)) {
                    return fail("opacity composite has negative region");
                }
                stack.backdrop_depth--;
                break;
            case DL_SAVE_BACKDROP:
                if (dl_has_negative_size(item->save_backdrop.w, item->save_backdrop.h)) {
                    return fail("saved backdrop has negative region");
                }
                stack.backdrop_depth++;
                break;
            case DL_APPLY_BLEND_MODE:
                if (stack.backdrop_depth <= 0) {
                    return fail("blend mode without saved backdrop");
                }
                if (dl_has_negative_size(item->apply_blend_mode.w, item->apply_blend_mode.h)) {
                    return fail("blend mode has negative region");
                }
                stack.backdrop_depth--;
                break;
            case DL_APPLY_FILTER:
                if (!dl_validate_resource_size(item->apply_filter.filter,
                                               item->apply_filter.w, item->apply_filter.h)) {
                    return fail("filter payload is invalid");
                }
                break;
            case DL_BOX_BLUR_REGION:
                if (dl_has_negative_size(item->box_blur_region.rw, item->box_blur_region.rh) ||
                    item->box_blur_region.blur_radius < 0.0f) {
                    return fail("box blur region payload is invalid");
                }
                break;
            case DL_BOX_BLUR_INSET:
                if (dl_has_negative_size(item->box_blur_inset.rw, item->box_blur_inset.rh) ||
                    item->box_blur_inset.pad < 0 ||
                    item->box_blur_inset.blur_radius < 0.0f) {
                    return fail("inset blur payload is invalid");
                }
                break;
            case DL_SHADOW_CLIP_SAVE:
                if (dl_has_negative_size(item->shadow_clip_save.rw, item->shadow_clip_save.rh)) {
                    return fail("shadow clip save has negative region");
                }
                stack.shadow_clip_depth++;
                break;
            case DL_SHADOW_CLIP_RESTORE:
                if (stack.shadow_clip_depth <= 0) {
                    return fail("shadow clip restore without save");
                }
                if (dl_has_negative_size(item->shadow_clip_restore.save_rw,
                                         item->shadow_clip_restore.save_rh)) {
                    return fail("shadow clip restore has negative region");
                }
                stack.shadow_clip_depth--;
                break;
            case DL_OUTER_SHADOW:
                if (dl_has_negative_size(item->outer_shadow.shadow_w, item->outer_shadow.shadow_h) ||
                    item->outer_shadow.blur_radius < 0.0f) {
                    return fail("outer shadow payload is invalid");
                }
                break;
            case DL_VIDEO_PLACEHOLDER:
                if (!dl_validate_resource_size(item->video_placeholder.video,
                                               item->video_placeholder.dst_w,
                                               item->video_placeholder.dst_h)) {
                    return fail("video placeholder payload is invalid");
                }
                break;
            case DL_WEBVIEW_LAYER_PLACEHOLDER:
                if (!dl_validate_resource_size(item->webview_layer_placeholder.surface,
                                               item->webview_layer_placeholder.dst_w,
                                               item->webview_layer_placeholder.dst_h)) {
                    return fail("webview placeholder payload is invalid");
                }
                break;
            case DL_BEGIN_ELEMENT: {
                int match = item->element_marker.matching_index;
                int scope_end = cursor.owner ? cursor.scope_size() : end;
                if (match <= i || match >= scope_end) {
                    return fail("element begin marker is not paired");
                }
                if (stack.element_depth >= DL_VALIDATE_ELEMENT_STACK_LIMIT) {
                    return fail("element marker nesting is too deep");
                }
                element_stack[stack.element_depth++] = {cursor.owner, i, match};
                break;
            }
            case DL_END_ELEMENT: {
                int match = item->element_marker.matching_index;
                if (stack.element_depth <= 0 || match < 0 || match >= i ||
                    element_stack[stack.element_depth - 1].owner != cursor.owner ||
                    element_stack[stack.element_depth - 1].begin != match ||
                    element_stack[stack.element_depth - 1].end != i) {
                    return fail("element end marker is not paired");
                }
                stack.element_depth--;
                break;
            }
        }
        if (validation) {
            if (!observed.properties.has_glyphs) observed.properties.has_glyphs = dl_item_contains_glyphs(item);
            observed.properties.has_video |= item->op == DL_VIDEO_PLACEHOLDER;
            observed.max_element_depth = max(observed.max_element_depth, stack.element_depth);
            observed.max_fragment_depth = max(observed.max_fragment_depth, cursor.depth);
        }
    }

    if (cursor.failed) return fail_at(cursor.root_index, "fragment graph is invalid or cyclic");

    if (stack.clip_depth != 0) {
        return fail_at(end, "clip stack is unbalanced");
    }
    if (stack.backdrop_depth != 0) {
        return fail_at(end, "backdrop stack is unbalanced");
    }
    if (stack.shadow_clip_depth != 0) {
        return fail_at(end, "shadow clip stack is unbalanced");
    }
    if (stack.element_depth != 0) {
        return fail_at(end, "element marker stack is unbalanced");
    }

    dl_validation_set(result, true, -1, "ok", 0, 0, 0, 0);
    if (validation) *validation = observed;
    return true;
}

bool dl_validate_range(const DisplayList* dl, int first, int end,
    DisplayListValidationResult* result, DisplayListProperties* properties) {
    dl_validation_set(result, true, -1, "ok", 0, 0, 0, 0);
    if (properties) *properties = {};
    if (!dl) return dl_validation_fail(result, -1, "display list is null", nullptr);
    if (dl->size() > dl->capacity())
        return dl_validation_fail(result, -1, "display list count/capacity is invalid", nullptr);
    if (dl->size() && !dl->data())
        return dl_validation_fail(result, -1, "display list has items but no storage", nullptr);
    if (first < 0 || end < first || end > dl->item_count())
        return dl_validation_fail(result, -1, "display list command range is invalid", nullptr);
    DisplayListCursor cursor(dl, first, end);
    DisplayChunkValidation validation;
    if (!dl_validate_stream(cursor, end, result, properties ? &validation : nullptr)) return false;
    if (properties) *properties = validation.properties;
    return true;
}

bool dl_validate_chunk(const DisplayPayloadOwner* chunk, DisplayListValidationResult* result,
    DisplayChunkValidation* validation) {
    if (validation) *validation = {};
    DisplayListCursor cursor(chunk);
    return dl_validate_stream(cursor, chunk ? chunk->command_count : 0, result, validation);
}

bool dl_validate(const DisplayList* dl, DisplayListValidationResult* result) {
    return dl_validate_range(dl, 0, dl ? dl->item_count() : 0, result);
}

bool dl_validate_range_or_log(const DisplayList* dl, int first, int end,
    const char* context, DisplayListProperties* properties) {
    DisplayListValidationResult result = {};
    if (dl_validate_range(dl, first, end, &result, properties)) return true;
    log_error("[DL_VALIDATE] %s: invalid display list at item %d: %s (clip=%d backdrop=%d shadow_clip=%d element=%d)",
              context ? context : "unknown",
              result.first_error_index,
              result.message ? result.message : "invalid",
              result.clip_depth,
              result.backdrop_depth,
              result.shadow_clip_depth,
              result.element_depth);
    return false;
}

bool dl_validate_or_log(const DisplayList* dl, const char* context) {
    return dl_validate_range_or_log(dl, 0, dl ? dl->item_count() : 0, context);
}

bool dl_validate_recording(DisplayList* list, int end, ValidatedDisplayList* recording,
    const char* context) {
    if (!recording) return false;
    *recording = {};
    DisplayListProperties properties = {};
    if (!dl_validate_range_or_log(list, 0, end, context, &properties)) return false;
    recording->list = lam::up(list); recording->storage = lam::up(list->data());
    recording->count = end; recording->properties = properties;
    return true;
}

bool dl_validate_append(DisplayList* list, ValidatedDisplayList* prefix,
    ValidatedDisplayList* recording, const char* context) {
    if (!recording || recording == prefix) return false;
    *recording = {};
    if (!list || !prefix || prefix->list != list || prefix->count > list->item_count()) {
        log_error("[DL_VALIDATE] %s: invalid recording prefix", context ? context : "unknown");
        return false;
    }
    // appending can relocate the array; validate the prefix again before exposing its new storage.
    if (prefix->storage != list->data() && !dl_validate_recording(list, prefix->count, prefix, context))
        return false;
    DisplayListProperties suffix = {};
    if (!dl_validate_range_or_log(list, prefix->count, list->item_count(), context, &suffix)) return false;
    *recording = *prefix;
    recording->count = list->item_count();
    recording->properties.has_glyphs |= suffix.has_glyphs;
    recording->properties.has_video |= suffix.has_video;
    return true;
}

bool dl_adopt_recording(DisplayList* dst, DisplayList* source,
    ValidatedDisplayList* recording, const char* context) {
    if (!dst || !source || dst == source || !recording || source->arena.arena ||
        source->arena.head || source->arena.open_scope || dst->arena.open_scope) return false;
    ValidatedDisplayList candidate;
    if (!dl_validate_recording(source, source->item_count(), &candidate, context)) return false;
    // scratch nodes register their object's address; only heap command/resource storage can move.
    dl_clear(dst);
    dst->lam::ArrayList<DisplayItem>::operator=(static_cast<lam::ArrayList<DisplayItem>&&>(*source));
    if (source->shared_arrays) {
        dl_shared_array_cache_destroy(dst->shared_arrays);
        dst->shared_arrays = source->shared_arrays; source->shared_arrays = nullptr;
    }
    candidate.list = lam::up(dst); candidate.storage = lam::up(dst->data());
    *recording = candidate;
    return true;
}

bool dl_item_is_retainable_for_fragment(const DisplayItem* item) {
    if (!item) return false;

    switch (item->op) {
        case DL_DRAW_IMAGE:
            // frame-local pixels are never retained
            if (item->draw_image.local_pixels ||
                !dl_retainable_surface(item, item->draw_image.resource,
                                       item->draw_image.resource_generation)) {
                return false;
            }
            break;
        case DL_DRAW_GLYPH:
            if (!dl_retainable_generation_resource(item->draw_glyph.bitmap.buffer,
                                                   item->draw_glyph.resource_generation)) {
                return false;
            }
            break;
        case DL_BLIT_SURFACE_SCALED:
            if (item->blit_surface_scaled.local_source ||
                !dl_retainable_surface(item, item->blit_surface_scaled.src_resource,
                                       item->blit_surface_scaled.src_generation)) {
                return false;
            }
            break;
        case DL_VIDEO_PLACEHOLDER:
            if (!dl_retainable_generation_resource(item->video_placeholder.video,
                                                   item->video_placeholder.video_generation)) {
                return false;
            }
            break;
        case DL_WEBVIEW_LAYER_PLACEHOLDER:
            if (!dl_retainable_surface(item->webview_layer_placeholder.surface,
                                       item->webview_layer_placeholder.resource,
                                       item->webview_layer_placeholder.surface_generation)) {
                return false;
            }
            break;
        case DL_APPLY_FILTER:
            if (item->apply_filter.filter) {
                return false;
            }
            break;
        case DL_COMPOSITE_OPACITY:
            if (item->composite_opacity.mask.gradient) return false;
            break;
        default:
            break;
    }
    return true;
}

static void dl_set_item_rect_bounds(DisplayItem* item,
                                    float x, float y, float w, float h) {
    if (!item) return;
    item->bounds[0] = x;
    item->bounds[1] = y;
    item->bounds[2] = w > 0.0f ? w : 0.0f;
    item->bounds[3] = h > 0.0f ? h : 0.0f;
}

static bool dl_union_item_bounds(float* left, float* top,
                                 float* right, float* bottom,
                                 const DisplayItem* item,
                                 bool has_bounds) {
    if (!item) return has_bounds;
    float w = item->bounds[2];
    float h = item->bounds[3];
    if (w <= 0.0f || h <= 0.0f) return has_bounds;
    float l = item->bounds[0];
    float t = item->bounds[1];
    float r = l + w;
    float b = t + h;
    if (!has_bounds) {
        *left = l;
        *top = t;
        *right = r;
        *bottom = b;
        return true;
    }
    if (l < *left) *left = l;
    if (t < *top) *top = t;
    if (r > *right) *right = r;
    if (b > *bottom) *bottom = b;
    return true;
}

Bound dl_content_bounds_range(const DisplayList* dl, int start, int end) {
    Bound bounds = {};
    bool valid = false;
    if (dl) for (int index = max(0, start); index < min(end, dl->item_count()); index++) {
        const DisplayItem* item = &dl->data()[index];
        if (dl_op_has_flags(item->op, DL_OP_FLAG_PRESERVES_REPLAY_STATE)) continue;
        valid = dl_union_item_bounds(&bounds.left, &bounds.top, &bounds.right, &bounds.bottom, item, valid);
    }
    return bounds;
}

Bound dl_content_bounds(const DisplayList* dl) {
    return dl_content_bounds_range(dl, 0, dl ? dl->item_count() : 0);
}

Bound dl_effect_capture_bounds(const DisplayList* dl, int start, int end, Bound bounds, float padding) {
    Bound source = dl_content_bounds_range(dl, start, end);
    if (source.right > source.left && source.bottom > source.top) {
        if (!(bounds.right > bounds.left && bounds.bottom > bounds.top))
            return {source.left - padding, source.top - padding, source.right + padding, source.bottom + padding};
        bounds.left = min(bounds.left, source.left - padding);
        bounds.top = min(bounds.top, source.top - padding);
        bounds.right = max(bounds.right, source.right + padding);
        bounds.bottom = max(bounds.bottom, source.bottom + padding);
    }
    return bounds;
}

bool dl_effect_bounds_to_region(const Bound* bounds, IRect* region) {
    if (!bounds || !region) return false;
    // an empty or inverted group covers no pixels; snapping outward first would
    // turn a sub-pixel inversion (an effect clipped away under a fractional
    // scale) into a 1 px region while its filter kept a negative size.
    if (!(bounds->right > bounds->left && bounds->bottom > bounds->top)) return false;
    float left = floorf(bounds->left), top = floorf(bounds->top);
    float right = ceilf(bounds->right), bottom = ceilf(bounds->bottom);
    if (!isfinite(left) || !isfinite(top) || !isfinite(right) || !isfinite(bottom) ||
            left < (float)INT_MIN || top < (float)INT_MIN || right >= (float)INT_MAX || bottom >= (float)INT_MAX ||
            !(right > left && bottom > top) || right - left >= (float)INT_MAX || bottom - top >= (float)INT_MAX) return false;
    region->x = (int)left; // INT_CAST_OK: effect bounds cover integer display-list pixel regions.
    region->y = (int)top; // INT_CAST_OK: effect bounds cover integer display-list pixel regions.
    region->w = (int)(right - left); // INT_CAST_OK: effect bounds cover integer display-list pixel regions.
    region->h = (int)(bottom - top); // INT_CAST_OK: effect bounds cover integer display-list pixel regions.
    return true;
}

bool dl_resize_saved_backdrops(DisplayList* dl, int start, int end, const Bound* bounds) {
    IRect region = {};
    if (!dl || !dl_effect_bounds_to_region(bounds, &region)) return false;
    // source recording is complete; resize the saved region before any replay/publication.
    for (int index = max(0, start); index < min(end, dl->item_count()); index++) {
        DisplayItem* item = &dl->data()[index];
        if (item->op != DL_SAVE_BACKDROP) continue;
        item->bounds[0] = (float)region.x; item->bounds[1] = (float)region.y;
        item->bounds[2] = (float)region.w; item->bounds[3] = (float)region.h;
        item->save_backdrop = {region.x, region.y, region.w, region.h};
    }
    return true;
}

int dl_begin_element(DisplayList* dl, uint32_t view_id,
                     float x, float y, float w, float h) {
    DisplayItem* item = dl_alloc_item(dl);
    if (!item) return -1;
    int index = dl->item_count() - 1;
    item->op = DL_BEGIN_ELEMENT;
    dl_set_item_rect_bounds(item, x, y, w, h);
    item->element_marker.view_id = view_id;
    item->element_marker.matching_index = -1;
    item->element_marker.marker_x = x;
    item->element_marker.marker_y = y;
    item->element_marker.marker_w = w;
    item->element_marker.marker_h = h;
    return index;
}

void dl_end_element(DisplayList* dl, int begin_index) {
    DisplayItem* end = dl_alloc_item(dl);
    if (!end) return;
    int end_index = dl->item_count() - 1;
    end->op = DL_END_ELEMENT;
    end->element_marker.view_id = 0;
    end->element_marker.matching_index = begin_index;

    if (!dl || begin_index < 0 || begin_index >= end_index) {
        return;
    }

    DisplayItem* begin = &dl->data()[begin_index];
    if (begin->op != DL_BEGIN_ELEMENT) {
        return;
    }

    float left = 0.0f, top = 0.0f, right = 0.0f, bottom = 0.0f;
    bool has_bounds = false;
    has_bounds = dl_union_item_bounds(&left, &top, &right, &bottom, begin, has_bounds);
    for (int i = begin_index + 1; i < end_index; i++) {
        has_bounds = dl_union_item_bounds(&left, &top, &right, &bottom,
                                          &dl->data()[i], has_bounds);
    }
    if (has_bounds) {
        dl_set_item_rect_bounds(begin, left, top, right - left, bottom - top);
        dl_set_item_rect_bounds(end, left, top, right - left, bottom - top);
    }
    begin->element_marker.matching_index = end_index;
    end->element_marker.view_id = begin->element_marker.view_id;
    end->element_marker.marker_x = begin->element_marker.marker_x;
    end->element_marker.marker_y = begin->element_marker.marker_y;
    end->element_marker.marker_w = begin->element_marker.marker_w;
    end->element_marker.marker_h = begin->element_marker.marker_h;
}

bool dl_draw_image_resolve(const DlDrawImage* item, DlResolvedImage* out) {
    if (!item || !out) return false;
    *out = {};
    if (item->resource.is_null()) {
        if (!item->local_pixels) return false;
        out->pixels = item->local_pixels;  // same kind: a frame-local borrow
        out->width = item->src_w; out->height = item->src_h; out->stride = item->src_stride;
        out->straight_alpha = item->straight_alpha;
        out->generation = item->resource_generation;
        return true;
    }
    ImageSurface* owner = image_surface_lookup(item->resource);
    if (!owner || !owner->pixels) return false;
    int width = owner->decoded_width > 0 ? owner->decoded_width : owner->width;
    int height = owner->decoded_height > 0 ? owner->decoded_height : owner->height;
    int stride = owner->pitch > 0 ? owner->pitch / 4 : width;
    if (width <= 0 || height <= 0 || stride < width) return false;
    int x = item->src_x, y = item->src_y, w = item->src_w, h = item->src_h;
    // a re-decode at another size keeps the recorded region in proportion
    if (item->surface_w > 0 && item->surface_h > 0 &&
        (width != item->surface_w || height != item->surface_h)) {
        x = (int)((int64_t)x * width / item->surface_w); // INT_CAST_OK: pixel coordinate inside the surface
        y = (int)((int64_t)y * height / item->surface_h); // INT_CAST_OK: pixel coordinate inside the surface
        w = (int)(((int64_t)w * width + item->surface_w / 2) / item->surface_w); // INT_CAST_OK: pixel extent inside the surface
        h = (int)(((int64_t)h * height + item->surface_h / 2) / item->surface_h); // INT_CAST_OK: pixel extent inside the surface
        if (x + w > width) w = width - x;
        if (y + h > height) h = height - y;
    }
    if (x < 0 || y < 0 || w <= 0 || h <= 0 || x + w > width || y + h > height) return false;
    out->pixels = lam::up((const uint32_t*)owner->pixels + (size_t)y * (size_t)stride + (size_t)x);
    out->width = w; out->height = h; out->stride = stride;
    out->straight_alpha = owner->alpha_mode == IMAGE_ALPHA_STRAIGHT;
    out->generation = owner->generation;
    out->owner = lam::up(owner);
    return true;
}

ImageSurface* dl_blit_source_resolve(const DlBlitSurfaceScaled* item) {
    if (!item) return nullptr;
    if (item->local_source) return item->local_source;
    return image_surface_lookup(item->src_resource);
}

// shared cloning/replacement preserves payload ownership for retained lists and deferred effects.
static bool dl_copy_clip_shape_stack(DisplayList* dst,
                                     lam::Up<const DlClipShapeStack>* out,
                                     const DlClipShapeStack* in) {
    if (!dst || !out) return false;
    *out = nullptr;
    if (!in || in->depth <= 0) return true;
    ClipShape shapes[RDT_MAX_CLIP_SHAPES];
    ClipShape* pointers[RDT_MAX_CLIP_SHAPES];
    int depth = dl_restore_clip_shapes(in, shapes, pointers);
    return dl_store_clip_shapes(dst, out, pointers, depth);
}

static bool dl_clone_path(lam::Own<RdtPath>* out, RdtPath* in) {
    if (!out) return false; *out = lam::own(in ? rdt_path_clone(in) : nullptr); return !in || *out;
}

static bool dl_copy_gradient_stops(DisplayList* dst, const DisplayList* source, lam::Shared<const RdtGradientStop>* out,
                                            const RdtGradientStop* stops, int count) {
    if (!out) return false; *out = lam::shared(dl_share_array(dst, source, stops, count, 1)); return !stops || count <= 0 || *out;
}

static bool dl_copy_dashes(DisplayList* dst, const DisplayList* source, lam::Shared<const float>* out, const float* dashes, int count) {
    if (!out) return false; *out = lam::shared(dl_share_array(dst, source, dashes, count, 2)); return !dashes || count <= 0 || *out;
}

static bool dl_clone_item_payload(DisplayList* dst, const DisplayList* source,
                                           DisplayItem* out,
                                           const DisplayItem* in,
                                           int source_start,
                                           int dest_start) {
    if (!dst || !out || !in) return false;
    switch (in->op) {
        case DL_DRAW_FRAGMENT:
            out->draw_fragment.owner = nullptr;
            if (!in->draw_fragment.owner || !ref_count_retain(&in->draw_fragment.owner->refs)) return false;
            out->draw_fragment.owner = in->draw_fragment.owner;
            break;
        case DL_DRAW_IMAGE:
            out->draw_image.snapshot_lease=lam::up(image_surface_snapshot_retain(in->draw_image.snapshot_lease));
            break;
        case DL_FILL_PATH:
            if (!dl_clone_path(&out->fill_path.path, in->fill_path.path)) return false;
            break;
        case DL_STROKE_PATH:
            if (!dl_clone_path(&out->stroke_path.path, in->stroke_path.path)) return false;
            if (!dl_copy_dashes(dst, source, &out->stroke_path.dash_array,
                    in->stroke_path.dash_array, in->stroke_path.dash_count)) return false;
            break;
        case DL_FILL_LINEAR_GRADIENT:
            if (!dl_clone_path(&out->fill_linear_gradient.path,
                    in->fill_linear_gradient.path)) return false;
            if (!dl_copy_gradient_stops(dst, source, &out->fill_linear_gradient.stops,
                    in->fill_linear_gradient.stops, in->fill_linear_gradient.stop_count)) return false;
            out->fill_linear_gradient.options.dash_array = lam::up(dl_share_array(dst, source,
                in->fill_linear_gradient.options.dash_array.get(), in->fill_linear_gradient.options.dash_count, 2));
            if (in->fill_linear_gradient.options.dash_count > 0 &&
                !out->fill_linear_gradient.options.dash_array) return false;
            break;
        case DL_FILL_RADIAL_GRADIENT:
            if (!dl_clone_path(&out->fill_radial_gradient.path,
                    in->fill_radial_gradient.path)) return false;
            if (!dl_copy_gradient_stops(dst, source, &out->fill_radial_gradient.stops,
                    in->fill_radial_gradient.stops, in->fill_radial_gradient.stop_count)) return false;
            out->fill_radial_gradient.options.dash_array = lam::up(dl_share_array(dst, source,
                in->fill_radial_gradient.options.dash_array.get(), in->fill_radial_gradient.options.dash_count, 2));
            if (in->fill_radial_gradient.options.dash_count > 0 &&
                !out->fill_radial_gradient.options.dash_array) return false;
            break;
        case DL_DRAW_PICTURE:
            out->draw_picture.picture = lam::counted(in->draw_picture.picture ?
                rdt_picture_dup(in->draw_picture.picture) : nullptr);
            if (in->draw_picture.picture && !out->draw_picture.picture) return false;
            break;
        case DL_PUSH_CLIP:
            if (!dl_clone_path(&out->push_clip.path, in->push_clip.path)) return false;
            break;
        case DL_FILL_SURFACE_RECT:
            if (!dl_copy_clip_shape_stack(dst, &out->fill_surface_rect.clip_shapes,
                                          in->fill_surface_rect.clip_shapes)) return false;
            break;
        case DL_BLIT_SURFACE_SCALED:
            out->blit_surface_scaled.snapshot_lease = lam::up(image_surface_snapshot_retain(in->blit_surface_scaled.snapshot_lease));
            if (!dl_copy_clip_shape_stack(dst, &out->blit_surface_scaled.clip_shapes,
                                          in->blit_surface_scaled.clip_shapes)) return false;
            break;
        case DL_BEGIN_ELEMENT:
            if (!dl_copy_semantic_group(dst, &out->element_marker.semantics,
                &in->element_marker.semantics)) return false;
            [[fallthrough]];
        case DL_END_ELEMENT:
            if (in->element_marker.matching_index >= source_start) {
                out->element_marker.matching_index =
                    dest_start + (in->element_marker.matching_index - source_start);
            }
            break;
        default:
            break;
    }
    return true;
}

void dl_truncate(DisplayList* dl, int target_count) {
    if (!dl || target_count < 0 || target_count > dl->item_count()) return;
    int current_count = dl->item_count();
    for (int i = target_count; i < current_count; i++) {
        dl_item_free_owned_payload(&dl->data()[i]);
    }
    dl->remove_range((size_t)target_count, (size_t)(current_count - target_count));
}

bool dl_copy_range(DisplayList* dst, const DisplayList* src,
                                   int start, int end) {
    if (!dst || !src || start < 0 || end < start || end >= src->item_count()) return false;

    int dest_start = dst->item_count();
    size_t required_count = 0;
    // copied fragments know their size; a frame-sized reserve per element exhausts paint budgets.
    if (!math_checked_add(dst->size(), (size_t)(end - start) + 1, &required_count) ||
        required_count > (size_t)INT_MAX ||
        (dst->capacity() == 0 && !dst->reserve(required_count))) return false;
    for (int i = start; i <= end; i++) {
        DisplayItem* out = dl_alloc_item(dst);
        if (!out) {
            dl_truncate(dst, dest_start);
            return false;
        }
        RENDER_METRIC_ADD(command_copy_bytes, sizeof(DisplayItem));
        DisplayItem copy = src->data()[i];
        *out = copy;
        if (!dl_clone_item_payload(dst, src, out, &src->data()[i], start, dest_start)) {
            dl_truncate(dst, dest_start);
            return false;
        }
    }
    return true;
}

bool dl_replace_tail(DisplayList* dst, int start, const DisplayList* src, int first, int last) {
    if (!dst || start < 0 || start > dst->item_count()) return false;
    int old_count = dst->item_count();
    // Clone before retiring the old tail: allocation failure preserves the
    // original stream, including its owned paths, pictures and image leases.
    if (!dl_copy_range(dst, src, first, last)) return false;
    int copied = dst->item_count() - old_count;
    for (int i = start; i < old_count; i++) dl_item_free_owned_payload(&dst->data()[i]);
    for (int i = old_count; i < old_count + copied; i++) {
        DisplayItem* item = &dst->data()[i];
        if ((item->op == DL_BEGIN_ELEMENT || item->op == DL_END_ELEMENT) &&
            item->element_marker.matching_index >= old_count)
            item->element_marker.matching_index += start - old_count;
    }
    memmove(dst->data() + start, dst->data() + old_count, (size_t)copied * sizeof(DisplayItem));
    // payload ownership moved with the commands; discarded trailing slots are
    // stale aliases and must not be passed through dl_truncate's payload free.
    dst->remove_range((size_t)(start + copied), (size_t)(old_count - start));
    return true;
}
