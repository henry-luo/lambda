#include "image_raster.hpp"
#include "image_process.hpp"
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
bool lambda_image_rect(Item value, int64_t* rect) {
    ItemReader input(value.to_const());
    if (!input.isArray() || input.asArray().length() != 4) return false;
    for (int i=0;i<4;i++) if (!lambda_item_to_int64_exact(input.asArray().get(i).item(),rect+i)) return false;
    return true;
}

bool lambda_image_setting(Item owner, const char* key, const char* expected) {
    Item value = item_attr(owner, key);
    return get_type_id(value) == LMD_TYPE_NULL ||
        (is_text_type_id(get_type_id(value)) && value.get_len() == strlen(expected) &&
            !memcmp(value.get_chars(), expected, value.get_len()));
}
bool lambda_image_color(Item value,uint8_t channels,uint8_t* samples) {
    ItemReader reader(value.to_const());
    if (!reader.isArray() || reader.asArray().length()!=channels) return false;
    for (uint8_t c=0;c<channels;c++) {
        double n;
        if (!item_try_to_double(reader.asArray().get(c).item(),&n) || !isfinite(n) || n<0 || n>1) return false;
        samples[c]=(uint8_t)lround(n*255);
    }
    return true;
}

static bool image_blend(Item owner, RasterBlend* mode) {
    Item value=item_attr(owner,"blend");
    if (get_type_id(value)==LMD_TYPE_NULL) { *mode=RASTER_NORMAL; return true; }
    const char* names[]={"normal","multiply","screen","overlay","darken","lighten","difference","exclusion"};
    if (!is_text_type_id(get_type_id(value))) return false;
    for (int i=0;i<8;i++) if (!strcmp(value.get_chars(),names[i])) { *mode=(RasterBlend)i; return true; }
    return false;
}

static bool image_affine(Item owner, double* matrix) {
    Item value=item_attr(owner,"transform");
    if (get_type_id(value)==LMD_TYPE_NULL) return true;
    ItemReader reader(value.to_const());
    if (!reader.isArray() || reader.asArray().length()!=6) return false;
    for (int i=0;i<6;i++) if (!item_try_to_double(reader.asArray().get(i).item(),matrix+i) ||
        !isfinite(matrix[i]) || fabs(matrix[i])>(i<4 ? 4096 : 1000000000)) return false;
    return fabs(matrix[0]*matrix[3]-matrix[1]*matrix[2])>=1e-12;
}
static bool image_decoration(Item node) {
    return lambda_image_tag(node,"mask") || lambda_image_tag(node,"effect") || lambda_image_tag(node,"style");
}

// effects preserve extent; geometry belongs to the retained affine transform.
static bool image_effect_options(Item effect, ImageProcessOptions* out) {
    if (!lambda_image_setting(effect,"kind","adjust")) return false;
    Item options=item_attr(effect,"options");
    if (get_type_id(options)!=LMD_TYPE_MAP) return false;
    const char* allowed[]={"exposure","brightness","contrast","saturation","temperature","gamma","black","white",
        "shadows","highlights","intensity","matrix","vignette","softness","sharpen"};
    // the iterator borrows its reader, which must outlive the full option scan.
    MapReader reader=MapReader::fromItem(options);
    auto entries=reader.entries(); const char* key; ItemReader value;
    while (entries.next(&key,&value)) {
        bool known=false;
        for (const char* name:allowed) if (!strcmp(name,key)) known=true;
        if (!known) return false;
    }
    return lambda_image_options(options,out);
}

struct ImageComposite {
    const char* failure = nullptr;
    Item assets = ItemNull;
    size_t scratch = 0;
    int64_t origin_x = 0, origin_y = 0;
    bool fail(const char* message) { failure = message; return false; }

    bool effects(Item owner, const RasterBuffer** pixels, RasterOwner& output) {
        for (int64_t i=0;i<owner.element->length;i++) {
            Item effect=item_at(owner,i);
            if (!lambda_image_tag(effect,"effect")) continue;
            Item enabled=item_attr(effect,"enabled");
            if (get_type_id(enabled)==LMD_TYPE_BOOL && !it2b(enabled)) continue;
            ImageProcessOptions config;
            if (!image_effect_options(effect,&config)) return fail("invalid enabled image effect or geometry option");
            size_t bytes=(size_t)(*pixels)->width*(*pixels)->height*4;
            if (!(*pixels)->pixels || (*pixels)->channels!=4) {
                if (bytes>256*1024*1024-scratch) return fail("effect scratch budget exceeded");
                RasterBuffer* dense=raster_buffer_create((*pixels)->width,(*pixels)->height,4,bytes);
                if (!dense) return fail("cannot allocate effect input");
                for (uint32_t y=0;y<dense->height;y++) for (uint32_t x=0;x<dense->width;x++)
                    raster_rgba_pixel(*pixels,x,y,dense->pixels+((size_t)y*dense->width+x)*4);
                scratch+=bytes; output.reset(dense); *pixels=dense;
            }
            if (bytes>256*1024*1024-scratch) return fail("effect scratch budget exceeded");
            RasterOwner next; next.result=raster_buffer_create((*pixels)->width,(*pixels)->height,4,bytes);
            if (!next.result) return fail("cannot allocate effect result");
            ImageProcessSource input; input.pixels=(*pixels)->pixels;
            input.width=(int)(*pixels)->width; input.height=(int)(*pixels)->height;
            input.strides[0]=(int64_t)input.width*4; input.strides[1]=4; input.strides[2]=1;
            ImageProcessResult processed;
            if (!image_process(input,config,&processed,nullptr,nullptr,next.result->pixels,bytes))
                return fail("cannot process image effect");
            if (output.result) scratch-=output.result->size;
            scratch+=bytes; output.reset(next.result); next.result=nullptr; *pixels=output.result;
        }
        return true;
    }

    bool children(RasterBuffer* target, Item owner, unsigned depth) {
        RootFrame roots(2); Rooted<Item> parent(roots,owner), child(roots,ItemNull);
        for (int64_t i=parent.get().element->length;i-- >0;) {
            child.set(item_at(parent.get(),i));
            if (image_decoration(child.get())) continue;
            Item clipping=item_attr(child.get(),"clipping");
            if (get_type_id(clipping)==LMD_TYPE_BOOL && it2b(clipping)) return fail("clipping layer requires a lower base sibling");
            int64_t above=i-1;
            while (above>=0 && image_decoration(item_at(parent.get(),above))) above--;
            bool chain=above>=0 && get_type_id(item_attr(item_at(parent.get(),above),"clipping"))==LMD_TYPE_BOOL &&
                it2b(item_attr(item_at(parent.get(),above),"clipping"));
            if (!chain) { if (!render(target,child.get(),depth+1)) return false; continue; }
            if (!lambda_image_setting(child.get(),"isolation","isolated"))
                return fail("pass-through clipping bases require explicit isolation");
            if (target->size>256*1024*1024-scratch) return fail("clipping scratch budget exceeded");
            RasterOwner clipped; clipped.result=raster_buffer_create(target->width,target->height,4,target->size);
            if (!clipped.result) return fail("cannot allocate clipping group");
            scratch+=clipped.result->size;
            RasterBlend blend;
            bool valid=image_blend(child.get(),&blend) && render(clipped.result,child.get(),depth+1);
            while (valid && above>=0) {
                child.set(item_at(parent.get(),above));
                if (image_decoration(child.get())) { above--; continue; }
                clipping=item_attr(child.get(),"clipping");
                if (get_type_id(clipping)!=LMD_TYPE_BOOL || !it2b(clipping)) break;
                valid=render(clipped.result,child.get(),depth+1,true); above--;
            }
            if (valid) raster_buffer_blend(target,clipped.result,0,0,nullptr,blend,1,nullptr,0,0,1,false,false);
            scratch-=clipped.result->size;
            if (!valid) return false;
            i=above+1;
        }
        return true;
    }

    bool render(RasterBuffer* target, Item source, unsigned depth, bool atop=false) {
        RootFrame roots(3);
        Rooted<Item> owner(roots, source), child(roots, ItemNull), mask_owner(roots, ItemNull);
        if (depth > 64) return fail("image composition nesting limit exceeded");
        const RasterBuffer* pixels = lambda_raster_buffer(owner.get());
        if (pixels) {
            if (!lambda_image_setting(owner.get(), "color", "srgb")) return fail("unsupported raster working color space");
            raster_buffer_over(target, pixels, -origin_x, -origin_y, 1, nullptr, 0, 0, 1, false); return true;
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
        RasterBlend blend;
        if (!image_blend(owner.get(),&blend) ||
                !lambda_image_setting(owner.get(), "composite", "source_over") ||
                (!lambda_image_setting(owner.get(), "isolation", "isolated") &&
                 !lambda_image_setting(owner.get(), "isolation", "pass_through")))
            return fail("unsupported blend, compositing operator or group isolation");
        double transform[]={1,0,0,1,0,0};
        if (!image_affine(owner.get(),transform)) return fail("invalid or singular layer affine transform");
        Item clipping=item_attr(owner.get(),"clipping");
        if (get_type_id(clipping)!=LMD_TYPE_NULL && get_type_id(clipping)!=LMD_TYPE_BOOL)
            return fail("layer clipping must be boolean");
        int64_t left = 0, top = 0, mask_left = 0, mask_top = 0;
        if (!lambda_image_position(owner.get(), &left, &top)) return fail("composition requires integral bounded positions");
        const RasterBuffer* mask = nullptr;
        double outside = 1;
        bool inverse = false;
        bool group = lambda_image_tag(owner.get(), "stack");
        if (!group && !lambda_image_setting(owner.get(),"isolation","isolated")) return fail("isolation belongs to groups");
        if (get_type_id(clipping)==LMD_TYPE_BOOL && it2b(clipping) && !atop)
            return fail("clipping layer requires a lower base sibling");
        if (!group && !lambda_image_tag(owner.get(), "layer")) return fail("expected a stack or layer");
        for (int64_t i = 0; i < owner.get().element->length; i++) {
            child.set(item_at(owner.get(), i));
            if (lambda_image_tag(child.get(), "effect")) {
                Item enabled = item_attr(child.get(), "enabled");
                if (get_type_id(enabled)!=LMD_TYPE_NULL && get_type_id(enabled)!=LMD_TYPE_BOOL)
                    return fail("effect enabled must be boolean");
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
            for (int64_t i=0;i<owner.get().element->length;i++) if (lambda_image_tag(item_at(owner.get(),i),"style"))
                return fail("layer styles on groups require explicit rasterization");
            if (left || top || get_type_id(item_attr(owner.get(),"transform"))!=LMD_TYPE_NULL)
                return fail("stack geometry must be expressed on descendant layers");
            // bound aggregate isolated-group scratch, not only each individual buffer.
            if (target->size > 256 * 1024 * 1024 - scratch) return fail("composition scratch budget exceeded");
            RasterBuffer* isolated = raster_buffer_create(target->width, target->height, 4, target->size);
            if (!isolated) return fail("cannot allocate isolated group");
            scratch += isolated->size;
            bool pass=!lambda_image_setting(owner.get(),"isolation","isolated");
            if (pass) memcpy(isolated->pixels,target->pixels,target->size);
            bool valid=children(isolated,owner.get(),depth);
            const RasterBuffer* rendered=isolated; RasterOwner filtered;
            if (valid && pass) {
                if (blend!=RASTER_NORMAL) valid=fail("pass-through group requires normal blend");
                for (int64_t i=0;valid && i<owner.get().element->length;i++) {
                    Item entry=item_at(owner.get(),i);
                    if (lambda_image_tag(entry,"effect")) {
                        Item enabled=item_attr(entry,"enabled");
                        if (get_type_id(enabled)!=LMD_TYPE_BOOL || it2b(enabled)) valid=fail("group effects require explicit isolation");
                    }
                }
                if (valid) raster_buffer_crossfade(target,rendered,opacity,mask,mask_left-origin_x,mask_top-origin_y,outside,inverse,atop);
            } else {
                if (valid) valid=effects(owner.get(),&rendered,filtered);
                if (valid) raster_buffer_blend(target,rendered,0,0,nullptr,blend,opacity,mask,
                    mask_left-origin_x,mask_top-origin_y,outside,inverse,atop);
            }
            if (filtered.result) scratch-=filtered.result->size;
            scratch -= isolated->size;
            raster_buffer_destroy(isolated);
            return valid;
        }
        Item kind = item_attr(owner.get(), "kind");
        bool empty = is_text_type_id(get_type_id(kind)) && !strcmp(kind.get_chars(), "empty");
        bool artwork=lambda_image_setting(owner.get(),"kind","text") || lambda_image_setting(owner.get(),"kind","vector");
        // a missing kind retains the original raster default.
        artwork=artwork && get_type_id(kind)!=LMD_TYPE_NULL;
        if (!empty && !artwork && get_type_id(kind) != LMD_TYPE_NULL &&
                (!is_text_type_id(get_type_id(kind)) || strcmp(kind.get_chars(), "raster")))
            return fail("specialized layer content is preserved but cannot be rendered by this profile");
        pixels = nullptr;
        RasterOwner primary;
        for (int64_t i = 0; i < owner.get().element->length; i++) {
            child.set(item_at(owner.get(), i));
            const RasterBuffer* candidate = lambda_raster_buffer(child.get());
            if (candidate || lambda_image_tag(child.get(),"text") || lambda_image_tag(child.get(),"vector")) {
                if (empty) return fail("empty layer cannot have primary raster content");
                if (pixels) return fail("raster layer has multiple primary rasters");
                if (artwork) {
                    if (!lambda_image_tag(child.get(),kind.get_chars())) return fail("artwork layer kind and content disagree");
                    if (!lambda_image_artwork(child.get(),assets,primary,256*1024*1024-scratch,&failure)) return false;
                    pixels=primary.result;
                } else if (!candidate) return fail("raster layer requires Raster content");
                else pixels = candidate;
                if (!lambda_image_setting(child.get(), "color", "srgb")) return fail("unsupported raster working color space");
            } else if (!image_decoration(child.get()))
                return fail("unsupported layer content");
        }
        if (!pixels) return empty || fail("raster layer has no primary raster");
        if (primary.result) {
            if (primary.result->size>256*1024*1024-scratch) return fail("artwork scratch budget exceeded");
            scratch+=primary.result->size;
        }
        RasterOwner filtered;
        if (!effects(owner.get(),&pixels,filtered)) return false;
        bool has_styles=false, styled=false;
        for (int64_t i=0;i<owner.get().element->length;i++) {
            Item style=item_at(owner.get(),i);
            if (lambda_image_tag(style,"style")) {
                has_styles=true; Item enabled=item_attr(style,"enabled");
                if (get_type_id(enabled)!=LMD_TYPE_BOOL || it2b(enabled)) styled=true;
            }
        }
        RasterOwner masked,styles; int offset=0;
        if (has_styles) {
            if (styled && mask) {
                size_t bytes=(size_t)pixels->width*pixels->height*4;
                if (bytes>256*1024*1024-scratch) return fail("style mask scratch budget exceeded");
                masked.result=raster_buffer_create(pixels->width,pixels->height,4,bytes);
                if (!masked.result) return fail("cannot allocate style mask input");
                raster_buffer_blend(masked.result,pixels,0,0,nullptr,RASTER_NORMAL,1,mask,mask_left-left,mask_top-top,outside,inverse,false);
                scratch+=bytes; pixels=masked.result; mask=nullptr;
            }
            if (!lambda_image_styles(owner.get(),&pixels,styles,&offset,256*1024*1024-scratch,&failure)) return false;
            // style padding is local to the primary; it follows the retained affine.
            transform[4]+=transform[0]*offset+transform[2]*offset;
            transform[5]+=transform[1]*offset+transform[3]*offset;
        }
        bool valid=raster_buffer_blend(target,pixels,left-origin_x,top-origin_y,transform,blend,opacity,
            mask,mask_left-left,mask_top-top,outside,inverse,atop);
        if (masked.result) scratch-=masked.result->size;
        if (filtered.result) scratch-=filtered.result->size;
        if (primary.result) scratch-=primary.result->size;
        return valid || fail("cannot composite affine layer");
    }
};

extern "C" Item fn_image_render(Item source, Item options) {
    GUARD_ERROR2(source, options);
    RootFrame roots(4);
    Rooted<Item> owner(roots, source), settings(roots, options), composition(roots, ItemNull), assets(roots,ItemNull);
    if (get_type_id(settings.get()) != LMD_TYPE_MAP) return lambda_image_error("render options must be a map");
    MapReader config = MapReader::fromItem(settings.get());
    auto entries = config.entries(); const char* key; ItemReader value;
    while (entries.next(&key, &value))
        if (strcmp(key, "frame") && strcmp(key, "width") && strcmp(key, "height") && strcmp(key, "rect"))
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
            else if (lambda_image_tag(child,"assets")) assets.set(child);
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
    ImageComposite renderer;
    renderer.assets=assets.get();
    if (!config.get("rect").isNull()) {
        int64_t rect[4];
        if (!lambda_image_rect(config.get("rect").item(),rect)) return lambda_image_error("render region requires four integer bounds");
        if (rect[0]<0 || rect[1]<0 || rect[2]<=rect[0] || rect[3]<=rect[1] || rect[2]>width || rect[3]>height)
            return lambda_image_error("render region is outside the canvas");
        renderer.origin_x=rect[0]; renderer.origin_y=rect[1]; width=rect[2]-rect[0]; height=rect[3]-rect[1];
    }
    RasterBuffer* target = raster_buffer_create((uint32_t)width, (uint32_t)height, 4, 64 * 1024 * 1024);
    if (!target) return lambda_image_error("cannot allocate composition result");
    if (!renderer.render(target, composition.get(), 0)) {
        raster_buffer_destroy(target);
        return lambda_image_error(renderer.failure);
    }
    Item color = {.item = s2it(heap_create_name("srgb", 4))};
    return lambda_raster_wrap(target, color);
}
