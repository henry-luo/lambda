#include "image_raster.hpp"
#include "value_builder.hpp"
#include "heap_api.h"
#include "lambda-number-runtime.hpp"
#include "../core/mark_reader.hpp"
#include "../../lib/image.h"
#include "../../lib/mem.h"

struct ImageDecodeOwner {
    uint8_t* bytes = nullptr;
    GifFrames* gif = nullptr;
    ImageDecoded decoded = {};
    ~ImageDecodeOwner() {
        image_gif_free(gif);
        image_decoded_dispose(&decoded);
        mem_free(bytes);
    }
};

static Item image_document_text(const char* text) {
    return {.item = s2it(heap_create_name(text, strlen(text)))};
}

extern "C" Item fn_image_decode(Item binary, Item options) {
    GUARD_ERROR2(binary, options);
    RootFrame roots(3);
    Rooted<Item> source(roots, binary), settings(roots, options), raster(roots, ItemNull);
    if (get_type_id(source.get()) != LMD_TYPE_BINARY || get_type_id(settings.get()) != LMD_TYPE_MAP)
        return lambda_image_error("decode requires encoded binary and an options map");
    MapReader config = MapReader::fromItem(settings.get());
    auto entries = config.entries(); const char* key; ItemReader value;
    while (entries.next(&key, &value))
        if (strcmp(key, "page_index") && strcmp(key, "normalize_orientation"))
            return lambda_image_error("unknown image decode option");
    ImageDecodeOptions decode; image_decode_options_init(&decode);
    if (!config.get("normalize_orientation").isNull()) {
        if (!config.get("normalize_orientation").isBool()) return lambda_image_error("normalize_orientation must be boolean");
        decode.normalize_orientation = config.get("normalize_orientation").asBool();
    }
    if (!config.get("page_index").isNull()) {
        int64_t page;
        if (!lambda_item_to_int64_exact(config.get("page_index").item(), &page) || page < 0 || page > UINT32_MAX)
            return lambda_image_error("page index must be a nonnegative integer");
        decode.page_index = (uint32_t)page;
    }
    ImageContext context; image_context_init(&context);
    size_t length = binary_length(source.get().get_binary());
    if (!length || length > context.limits.max_encoded_bytes) return lambda_image_error("encoded image exceeds source limit");
    ImageDecodeOwner owner;
    owner.bytes = (uint8_t*)mem_alloc(length, MEM_CAT_IMAGE);
    if (!owner.bytes) return ItemError;
    // decoder input stays stable while publishing GC-managed frame values.
    memcpy(owner.bytes, binary_data(source.get().get_binary()), length);
    if (length >= 4 && !memcmp(owner.bytes, "8BPS", 4)) {
        if (decode.page_index) return lambda_image_error("PSD has no page index in this profile");
        return lambda_image_decode_psd(owner.bytes, length);
    }
    ImageSource input = image_source_memory(owner.bytes, length);
    ImageInfo info = {};
    ImageError error = {};
    if (image_probe(&context, &input, &info, &error) != IMAGE_OK) return lambda_image_error(error.message);
    if (info.format != IMAGE_CODEC_GIF && info.frames_known && info.frame_count > 1)
        return lambda_image_error("this animated source requires a later frame import profile");
    uint32_t count = 1;
    if (info.format == IMAGE_CODEC_GIF) {
        int frames = image_gif_frame_count_from_memory(owner.bytes, length);
        if (frames <= 0) return lambda_image_error("invalid GIF frame table");
        count = (uint32_t)frames;
    }
    if (!count || count > context.limits.max_frames || (uint64_t)info.width * info.height > 16000000 ||
            (uint64_t)info.width * info.height * 4 * count > context.limits.max_decoded_bytes)
        return lambda_image_error("decoded image frames exceed the image budget");
    if (count > 1) {
        owner.gif = image_gif_load_from_memory(owner.bytes, length);
        if (!owner.gif || owner.gif->frame_count != (int)count) return lambda_image_error("GIF frame normalization failed");
    }
    RootedValueBuilder frames(true), document;
    uint32_t width = decode.normalize_orientation ? info.display_width : info.width;
    uint32_t height = decode.normalize_orientation ? info.display_height : info.height;
    if (!owner.gif) {
        if (image_decode(&context, &input, &decode, &owner.decoded, &error) != IMAGE_OK)
            return lambda_image_error(error.message);
        // a selected page/representation may have a different extent from the probe's default.
        width = owner.decoded.width; height = owner.decoded.height;
        if ((uint64_t)width * height > 16000000) return lambda_image_error("selected image exceeds the raster pixel budget");
    }
    for (uint32_t i = 0; i < count; i++) {
        RasterBuffer* buffer = raster_buffer_create(width, height, 4, 64 * 1024 * 1024);
        if (!buffer) return lambda_image_error("cannot allocate imported raster");
        uint32_t delay = 0;
        if (owner.gif) {
            const uint32_t* pixels = owner.gif->frames[i].pixels;
            // unpack the legacy GIF color words explicitly, independent of host endian.
            for (size_t p = 0; p < buffer->size / 4; p++)
                for (unsigned c = 0; c < 4; c++) buffer->pixels[p * 4 + c] = (uint8_t)(pixels[p] >> (c * 8));
            delay = (uint32_t)owner.gif->frames[i].source_delay_ms;
        } else {
            if (owner.decoded.width != buffer->width || owner.decoded.height != buffer->height) {
                raster_buffer_destroy(buffer); return lambda_image_error("decoded image extent changed unexpectedly");
            }
            for (uint32_t y = 0; y < buffer->height; y++)
                memcpy(buffer->pixels + (size_t)y * buffer->width * 4,
                    owner.decoded.pixels + y * owner.decoded.stride, (size_t)buffer->width * 4);
            delay = info.format == IMAGE_CODEC_GIF ? owner.decoded.source_delay_ms : owner.decoded.delay_ms;
        }
        const ImageInfo& frame_info = owner.gif ? info : owner.decoded.info;
        raster.set(lambda_raster_wrap(buffer, image_document_text(frame_info.has_icc ? "unknown" : "srgb")));
        if (item_is_error(raster.get())) return raster.get();
        RootedValueBuilder frame, duration;
        duration.put("ticks", {.item = i2it(delay)});
        duration.put("rate", {.item = i2it(1000)});
        frame.put("raster", raster.get());
        frame.put("duration", duration.value.get());
        frames.append(frame.value.get());
    }
    uint32_t plays = 1;
    if (owner.gif && owner.gif->has_loop_extension)
        plays = owner.gif->loop_count ? (uint32_t)owner.gif->loop_count + 1 : 0;
    else if (info.format == IMAGE_CODEC_GIF && owner.decoded.has_loop_extension)
        plays = owner.decoded.loop_count ? owner.decoded.loop_count + 1 : 0;
    document.put("width", {.item = i2it(width)});
    document.put("height", {.item = i2it(height)});
    document.put("plays", {.item = i2it(plays)});
    document.put("animated", {.item = b2it(info.format == IMAGE_CODEC_GIF)});
    document.put("frames", frames.value.get());
    const ImageCapabilities* codec = image_codec_capabilities(info.format);
    document.put("format", image_document_text(codec ? codec->name : "unknown"));
    return document.value.get();
}

static Item image_encode_gif(Item source, Item composites, Item options) {
    RootFrame roots(3);
    Rooted<Item> image(roots, source), rasters(roots, composites), settings(roots, options);
    if (get_type_id(rasters.get()) != LMD_TYPE_ARRAY || !rasters.get().array->length || rasters.get().array->length > 4096)
        return lambda_image_error("GIF export requires a bounded raster frame array");
    MapReader config = MapReader::fromItem(settings.get());
    if (!config.get("quality").isNull() || !config.get("lossless").isNull())
        return lambda_image_error("GIF exact-palette export does not accept quality/lossless options");
    uint32_t count = (uint32_t)rasters.get().array->length;
    const RasterBuffer** frames = (const RasterBuffer**)mem_calloc(count, sizeof(RasterBuffer*), MEM_CAT_IMAGE);
    uint16_t* delays = (uint16_t*)mem_calloc(count, sizeof(uint16_t), MEM_CAT_IMAGE);
    if (!frames || !delays) { mem_free(frames); mem_free(delays); return ItemError; }
    const char* failure = nullptr;
    for (uint32_t i = 0; i < count; i++) {
        Item frame = item_at(rasters.get(), i);
        frames[i] = lambda_raster_buffer(frame);
        if (!frames[i] || !lambda_image_setting(frame, "color", "srgb")) { failure = "GIF frame requires an sRGB Raster"; break; }
    }
    int64_t plays = 1;
    Item loops = item_attr(image.get(), "plays");
    if (get_type_id(loops) != LMD_TYPE_NULL && (!lambda_item_to_int64_exact(loops, &plays) || plays < 0 || plays > 65536))
        failure = "GIF plays must be an integer in [0,65536]";
    int64_t selected = -1;
    if (!config.get("frame").isNull()) {
        if (!lambda_item_to_int64_exact(config.get("frame").item(), &selected) || selected < 0)
            failure = "invalid GIF frame selection";
        plays = 1;
    }
    uint32_t output_index = 0;
    int64_t source_index = 0;
    if (!failure && lambda_image_tag(image.get(), "image"))
        for (int64_t i = 0; i < image.get().element->length; i++) {
            Item frame = item_at(image.get(), i);
            if (!lambda_image_tag(frame, "frame")) continue;
            int64_t index = source_index++;
            if (selected >= 0 && selected != index) continue;
            Item duration = item_attr(frame, "duration");
            int64_t ticks, rate;
            if (!lambda_item_to_int64_exact(item_attr(duration, "ticks"), &ticks) ||
                    !lambda_item_to_int64_exact(item_attr(duration, "rate"), &rate) || ticks < 0 || rate <= 0) {
                failure = "GIF frame duration requires nonnegative ticks and a positive rate"; break;
            }
            uint64_t a = (uint64_t)rate, b = 100;
            while (b) { uint64_t remainder = a % b; a = b; b = remainder; }
            uint64_t divisor = (uint64_t)rate / a, multiplier = 100 / a;
            if ((uint64_t)ticks % divisor || (uint64_t)ticks / divisor > 65535 / multiplier || output_index >= count) {
                failure = "GIF duration must be an exact centisecond value in [0,65535]"; break;
            }
            delays[output_index++] = (uint16_t)((uint64_t)ticks / divisor * multiplier);
        }
    if (!failure && source_index && output_index != count) failure = "GIF frame raster/timing counts differ";
    ImageEncoded encoded = {}; ImageError error = {};
    if (!failure && image_encode_gif_frames(nullptr, frames, delays, count, (uint32_t)plays, &encoded, &error) != IMAGE_OK)
        failure = error.message;
    mem_free(frames); mem_free(delays);
    if (failure) return lambda_image_error(failure);
    Binary* result = heap_binary_from_bytes((const char*)encoded.data, (int64_t)encoded.length);
    image_free(encoded.data);
    return result ? Item{.item = x2it(result)} : ItemError;
}

extern "C" Item fn_image_encode(Item source, Item composite, Item options) {
    GUARD_ERROR3(source, composite, options);
    RootFrame roots(3);
    Rooted<Item> image(roots, source), merged(roots, composite), settings(roots, options);
    if (get_type_id(settings.get()) != LMD_TYPE_MAP) return lambda_image_error("encode options must be a map");
    MapReader config = MapReader::fromItem(settings.get());
    auto entries = config.entries(); const char* key; ItemReader value;
    while (entries.next(&key, &value))
        if (strcmp(key, "format") && strcmp(key, "frame") && strcmp(key, "quality") && strcmp(key, "lossless"))
            return lambda_image_error("unknown image encode option");
    ItemReader format = config.get("format");
    if (!format.isNull() && !format.isString() && !format.isSymbol())
        return lambda_image_error("image export format must be text");
    const char* name = format.isNull() ? "png" : format.item().get_chars();
    if (!format.isNull() && format.item().get_len() != strlen(name))
        return lambda_image_error("image export format must be a complete format name");
    if (!strcmp(name, "gif")) return image_encode_gif(image.get(), merged.get(), settings.get());
    const RasterBuffer* pixels = lambda_raster_buffer(merged.get());
    if (!pixels || pixels->channels != 4 || !lambda_image_setting(merged.get(), "color", "srgb"))
        return lambda_image_error("encode requires an sRGB RGBA8 raster");
    int64_t frame = 0, frames = 0;
    if (!config.get("frame").isNull() && !lambda_item_to_int64_exact(config.get("frame").item(), &frame))
        return lambda_image_error("export frame must be an integer");
    if (lambda_image_tag(image.get(), "image"))
        for (int64_t i = 0; i < image.get().element->length; i++)
            if (lambda_image_tag(item_at(image.get(), i), "frame")) frames++;
    if (frames > 1 && config.get("frame").isNull())
        return lambda_image_error("static/layered export requires an explicit animation frame selection");
    if (!strcmp(name, "psd")) {
        if (!config.get("quality").isNull() || !config.get("lossless").isNull())
            return lambda_image_error("PSD raw-plane export does not accept quality/lossless options");
        return lambda_image_encode_psd(image.get(), merged.get(), frame);
    }
    ImageCodecFormat codec = IMAGE_CODEC_UNKNOWN;
    for (int c = IMAGE_CODEC_PNG; c <= IMAGE_CODEC_ICO; c++) {
        const ImageCapabilities* cap = image_codec_capabilities((ImageCodecFormat)c);
        if (cap && !strcmp(cap->name, name)) codec = (ImageCodecFormat)c;
    }
    if (codec == IMAGE_CODEC_UNKNOWN) return lambda_image_error("unknown or unavailable image writer");
    ImageContext context; image_context_init(&context);
    ImageEncodeOptions encode; image_encode_options_init(&encode, codec);
    if (!config.get("quality").isNull()) {
        int64_t quality;
        if (!lambda_item_to_int64_exact(config.get("quality").item(), &quality) || quality < 0 || quality > 100)
            return lambda_image_error("image quality must be an integer in [0,100]");
        encode.quality = (int)quality;
    }
    if (!config.get("lossless").isNull()) {
        if (!config.get("lossless").isBool()) return lambda_image_error("lossless must be boolean");
        encode.lossless = config.get("lossless").asBool();
    }
    ImagePixelView view = {pixels->pixels, pixels->size, (size_t)pixels->width * 4,
        pixels->width, pixels->height, 4, false};
    ImageEncoded bytes = {}; ImageError error = {};
    if (image_encode_to_memory(&context, &view, &encode, &bytes, &error) != IMAGE_OK)
        return lambda_image_error(error.message);
    Binary* result = heap_binary_from_bytes((const char*)bytes.data, (int64_t)bytes.length);
    image_free(bytes.data);
    return result ? Item{.item = x2it(result)} : ItemError;
}

extern "C" Item fn_image_capabilities() {
    ImageContext context; image_context_init(&context);
    RootedValueBuilder formats(true);
    for (int c = IMAGE_CODEC_PNG; c <= IMAGE_CODEC_ICO; c++) {
        const ImageCapabilities* cap = image_codec_capabilities((ImageCodecFormat)c);
        if (!cap) continue;
        RootedValueBuilder format;
        format.put("format", image_document_text(cap->name));
        format.put("read", {.item = b2it(cap->read)});
        format.put("write", {.item = b2it(cap->write || c == IMAGE_CODEC_GIF)});
        format.put("animated_read", {.item = b2it(c == IMAGE_CODEC_GIF)});
        format.put("animated_write", {.item = b2it(c == IMAGE_CODEC_GIF)});
        format.put("editable_layers", {.item = b2it(false)});
        format.put("max_frames", {.item = i2it(context.limits.max_frames)});
        format.put("decoded_byte_limit", {.item = i2it(context.limits.max_decoded_bytes)});
        if (c == IMAGE_CODEC_GIF) format.put("profile", image_document_text("exact_palette_255_binary_alpha_centiseconds"));
        else format.put("profile", image_document_text("rgba8_still"));
        formats.append(format.value.get());
    }
    RootedValueBuilder psd;
    psd.put("format", image_document_text("psd"));
    psd.put("read", {.item = b2it(true)}); psd.put("write", {.item = b2it(true)});
    psd.put("animated_read", {.item = b2it(false)}); psd.put("animated_write", {.item = b2it(false)});
    psd.put("editable_layers", {.item = b2it(true)});
    psd.put("profile", image_document_text("version1_rgb_gray8_raw_rle_zip_prediction_read_raw_write"));
    formats.append(psd.value.get());
    return formats.value.get();
}
