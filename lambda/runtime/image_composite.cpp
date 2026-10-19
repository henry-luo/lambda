#include "image_raster.hpp"
#include "transpiler.hpp"
#include "lambda-number-runtime.hpp"
#include "../core/mark_reader.hpp"
#include <math.h>

bool lambda_image_tag(Item item, const char* tag) {
    if (get_type_id(item) != LMD_TYPE_ELEMENT || !item.element->type) return false;
    TypeElmt* type = (TypeElmt*)item.element->type;
    return type->name.length == strlen(tag) && !memcmp(type->name.str, tag, type->name.length);
}

bool lambda_image_number(Item owner, const char* key, double fallback, double* out) {
    Item value = item_attr(owner, key);
    if (get_type_id(value) == LMD_TYPE_NULL) { *out = fallback; return true; }
    return item_try_to_double(value, out) && isfinite(*out);
}

bool lambda_image_position(Item owner, int64_t* left, int64_t* top) {
    Item value = item_attr(owner, "position");
    *left = *top = 0;
    if (get_type_id(value) == LMD_TYPE_NULL) return true;
    ItemReader point(value.to_const());
    if (!point.isArray() || point.asArray().length() != 2) return false;
    double coordinates[2];
    for (size_t i = 0; i < 2; i++)
        if (!item_try_to_double(point.asArray().get(i).item(), &coordinates[i]) ||
                !isfinite(coordinates[i]) || floor(coordinates[i]) != coordinates[i] ||
                fabs(coordinates[i]) > 1000000000) return false;
    *left = (int64_t)coordinates[0]; *top = (int64_t)coordinates[1];
    return true;
}

bool lambda_image_setting(Item owner, const char* key, const char* expected) {
    Item value = item_attr(owner, key);
    return get_type_id(value) == LMD_TYPE_NULL ||
        (is_text_type_id(get_type_id(value)) && value.get_len() == strlen(expected) &&
            !memcmp(value.get_chars(), expected, value.get_len()));
}

struct ImageComposite {
    const char* failure = nullptr;
    size_t scratch = 0;
    bool fail(const char* message) { failure = message; return false; }

    bool render(RasterBuffer* target, Item source, unsigned depth) {
        RootFrame roots(3);
        Rooted<Item> owner(roots, source), child(roots, ItemNull), mask_owner(roots, ItemNull);
        if (depth > 64) return fail("image composition nesting limit exceeded");
        const RasterBuffer* pixels = lambda_raster_buffer(owner.get());
        if (pixels) {
            if (!lambda_image_setting(owner.get(), "color", "srgb")) return fail("unsupported raster working color space");
            raster_buffer_over(target, pixels, 0, 0, 1, nullptr, 0, 0, 1, false); return true;
        }
        if (get_type_id(owner.get()) != LMD_TYPE_ELEMENT) return fail("composition content must be an element");
        Item unsupported = item_attr(owner.get(), "unsupported");
        if (get_type_id(unsupported) == LMD_TYPE_BOOL && it2b(unsupported))
            return fail("source composition contains preserved unsupported features");
        Item visible = item_attr(owner.get(), "visible");
        if (get_type_id(visible) != LMD_TYPE_NULL && get_type_id(visible) != LMD_TYPE_BOOL)
            return fail("layer visibility must be boolean");
        if (get_type_id(visible) == LMD_TYPE_BOOL && !it2b(visible)) return true;
        double opacity;
        if (!lambda_image_number(owner.get(), "opacity", 1, &opacity) || opacity < 0 || opacity > 1)
            return fail("layer opacity must be in [0,1]");
        if (!lambda_image_setting(owner.get(), "blend", "normal") ||
                !lambda_image_setting(owner.get(), "composite", "source_over") ||
                !lambda_image_setting(owner.get(), "isolation", "isolated"))
            return fail("unsupported blend, compositing operator or group isolation");
        if (get_type_id(item_attr(owner.get(), "transform")) != LMD_TYPE_NULL ||
                get_type_id(item_attr(owner.get(), "clipping")) != LMD_TYPE_NULL)
            return fail("affine transforms and clipping are not supported by this composition profile");
        int64_t left = 0, top = 0, mask_left = 0, mask_top = 0;
        if (!lambda_image_position(owner.get(), &left, &top)) return fail("composition requires integral bounded positions");
        const RasterBuffer* mask = nullptr;
        double outside = 1;
        bool inverse = false;
        bool group = lambda_image_tag(owner.get(), "stack");
        if (!group && !lambda_image_tag(owner.get(), "layer")) return fail("expected a stack or layer");
        for (int64_t i = 0; i < owner.get().element->length; i++) {
            child.set(item_at(owner.get(), i));
            if (lambda_image_tag(child.get(), "effect")) {
                Item enabled = item_attr(child.get(), "enabled");
                if (get_type_id(enabled) != LMD_TYPE_BOOL || it2b(enabled))
                    return fail("enabled effect requires explicit baking before this render profile");
            } else if (lambda_image_tag(child.get(), "mask")) {
                Item enabled = item_attr(child.get(), "enabled");
                if (get_type_id(enabled) != LMD_TYPE_NULL && get_type_id(enabled) != LMD_TYPE_BOOL)
                    return fail("mask enabled must be boolean");
                if (!lambda_image_setting(child.get(), "role", "alpha")) return fail("unsupported mask role");
                if (get_type_id(enabled) == LMD_TYPE_BOOL && !it2b(enabled)) continue;
                if (mask) return fail("multiple masks are not supported by this render profile");
                if (!lambda_image_position(child.get(), &mask_left, &mask_top) ||
                        !lambda_image_number(child.get(), "default", 1, &outside) || outside < 0 || outside > 1)
                    return fail("invalid mask position or outside coverage");
                mask_left += left; mask_top += top;
                Item invert = item_attr(child.get(), "invert");
                if (get_type_id(invert) != LMD_TYPE_NULL && get_type_id(invert) != LMD_TYPE_BOOL)
                    return fail("mask inversion must be boolean");
                inverse = get_type_id(invert) == LMD_TYPE_BOOL && it2b(invert);
                for (int64_t m = 0; m < child.get().element->length; m++) {
                    Item candidate = item_at(child.get(), m);
                    if (!lambda_raster_buffer(candidate) || get_type_id(mask_owner.get()) != LMD_TYPE_NULL)
                        return fail("mask requires exactly one Raster");
                    mask_owner.set(candidate);
                }
                mask = lambda_raster_buffer(mask_owner.get());
                if (!mask || mask->channels != 1) return fail("alpha mask requires one gray channel");
            }
        }
        if (group) {
            if (left || top) return fail("stack positions must be expressed on descendant layers");
            // bound aggregate isolated-group scratch, not only each individual buffer.
            if (target->size > 256 * 1024 * 1024 - scratch) return fail("composition scratch budget exceeded");
            RasterBuffer* isolated = raster_buffer_create(target->width, target->height, 4, target->size);
            if (!isolated) return fail("cannot allocate isolated group");
            scratch += isolated->size;
            bool valid = true;
            for (int64_t i = owner.get().element->length; i-- > 0 && valid;) {
                child.set(item_at(owner.get(), i));
                if (lambda_image_tag(child.get(), "mask") || lambda_image_tag(child.get(), "effect")) continue;
                valid = render(isolated, child.get(), depth + 1);
            }
            if (valid) raster_buffer_over(target, isolated, 0, 0, opacity, mask, mask_left, mask_top, outside, inverse);
            scratch -= isolated->size;
            raster_buffer_destroy(isolated);
            return valid;
        }
        Item kind = item_attr(owner.get(), "kind");
        bool empty = is_text_type_id(get_type_id(kind)) && !strcmp(kind.get_chars(), "empty");
        if (!empty && get_type_id(kind) != LMD_TYPE_NULL &&
                (!is_text_type_id(get_type_id(kind)) || strcmp(kind.get_chars(), "raster")))
            return fail("specialized layer content is preserved but cannot be rendered by this profile");
        pixels = nullptr;
        for (int64_t i = 0; i < owner.get().element->length; i++) {
            child.set(item_at(owner.get(), i));
            const RasterBuffer* candidate = lambda_raster_buffer(child.get());
            if (candidate) {
                if (empty) return fail("empty layer cannot have primary raster content");
                if (pixels) return fail("raster layer has multiple primary rasters");
                pixels = candidate;
                if (!lambda_image_setting(child.get(), "color", "srgb")) return fail("unsupported raster working color space");
            } else if (!lambda_image_tag(child.get(), "mask") && !lambda_image_tag(child.get(), "effect"))
                return fail("unsupported layer content");
        }
        if (!pixels) return empty || fail("raster layer has no primary raster");
        raster_buffer_over(target, pixels, left, top, opacity, mask, mask_left, mask_top, outside, inverse);
        return true;
    }
};

extern "C" Item fn_image_render(Item source, Item options) {
    GUARD_ERROR2(source, options);
    RootFrame roots(3);
    Rooted<Item> owner(roots, source), settings(roots, options), composition(roots, ItemNull);
    if (get_type_id(settings.get()) != LMD_TYPE_MAP) return lambda_image_error("render options must be a map");
    MapReader config = MapReader::fromItem(settings.get());
    auto entries = config.entries(); const char* key; ItemReader value;
    while (entries.next(&key, &value))
        if (strcmp(key, "frame") && strcmp(key, "width") && strcmp(key, "height"))
            return lambda_image_error("unknown image render option");
    double width, height;
    if (!lambda_image_number(owner.get(), "width", 0, &width) || !lambda_image_number(owner.get(), "height", 0, &height))
        return lambda_image_error("invalid image dimensions");
    if (!config.get("width").isNull()) {
        if (!item_try_to_double(config.get("width").item(), &width)) return lambda_image_error("invalid render width");
    }
    if (!config.get("height").isNull()) {
        if (!item_try_to_double(config.get("height").item(), &height)) return lambda_image_error("invalid render height");
    }
    if (!isfinite(width) || !isfinite(height) || width < 1 || height < 1 || width > 16000000 ||
            height > 16000000 || floor(width) != width || floor(height) != height || width * height > 16000000)
        return lambda_image_error("render requires bounded positive image dimensions");
    int64_t frame = 0;
    if (!config.get("frame").isNull() && !lambda_item_to_int64_exact(config.get("frame").item(), &frame))
        return lambda_image_error("frame index must be an integer");
    if (frame < 0) return lambda_image_error("frame index is out of bounds");
    if (frame && !lambda_image_tag(owner.get(), "image"))
        return lambda_image_error("standalone composition has only frame zero");
    composition.set(owner.get());
    if (lambda_image_tag(owner.get(), "image")) {
        Item unsupported = item_attr(owner.get(), "unsupported");
        if (get_type_id(unsupported) == LMD_TYPE_BOOL && it2b(unsupported))
            return lambda_image_error("source image contains preserved unsupported features");
        composition.set(ItemNull);
        int64_t frame_count = 0, stack_count = 0;
        for (int64_t i = 0; i < owner.get().element->length; i++) {
            Item child = item_at(owner.get(), i);
            if (lambda_image_tag(child, "stack")) { stack_count++; if (!frame) composition.set(child); }
            else if (lambda_image_tag(child, "frame")) { if (frame_count++ == frame) composition.set(child); }
            else if (!lambda_image_tag(child, "assets") && !lambda_image_tag(child, "metadata") && !lambda_image_tag(child, "preview"))
                return lambda_image_error("unexpected image child");
        }
        if (stack_count > 1 || (stack_count && frame_count) || get_type_id(composition.get()) == LMD_TYPE_NULL)
            return lambda_image_error("image requires one root stack or an in-range animation frame");
    }
    if (lambda_image_tag(composition.get(), "frame")) {
        if (composition.get().element->length != 1 || !lambda_image_tag(item_at(composition.get(), 0), "stack"))
            return lambda_image_error("frame requires exactly one root stack");
        composition.set(item_at(composition.get(), 0));
    }
    RasterBuffer* target = raster_buffer_create((uint32_t)width, (uint32_t)height, 4, 64 * 1024 * 1024);
    if (!target) return lambda_image_error("cannot allocate composition result");
    ImageComposite renderer;
    if (!renderer.render(target, composition.get(), 0)) {
        raster_buffer_destroy(target);
        return lambda_image_error(renderer.failure);
    }
    Item color = {.item = s2it(heap_create_name("srgb", 4))};
    return lambda_raster_wrap(target, color);
}
