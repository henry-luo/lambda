#include "image.h"
#include "hashmap.h"
#include "strbuf.h"
#include "mem.h"
#include <gif_lib.h>

struct GifPaletteEntry { uint32_t color; uint8_t index; };
static uint64_t gif_palette_hash(const void* entry, uint64_t seed0, uint64_t seed1) {
    return hashmap_sip(&((const GifPaletteEntry*)entry)->color, sizeof(uint32_t), seed0, seed1);
}
static int gif_palette_compare(const void* a, const void* b, void*) {
    uint32_t x = ((const GifPaletteEntry*)a)->color, y = ((const GifPaletteEntry*)b)->color;
    return x < y ? -1 : x != y;
}

struct GifOutput {
    StrBuf* bytes;
    size_t limit;
    bool failed = false;
};
static int gif_write_bytes(GifFileType* gif, const GifByteType* data, int count) {
    GifOutput* output = (GifOutput*)gif->UserData;
    if (count < 0 || output->failed || (size_t)count > output->limit - output->bytes->length ||
            !strbuf_ensure_cap(output->bytes, output->bytes->length + (size_t)count + 1)) {
        output->failed = true; return 0;
    }
    memcpy(output->bytes->str + output->bytes->length, data, (size_t)count);
    output->bytes->length += (size_t)count; output->bytes->str[output->bytes->length] = 0;
    return count;
}

static ImageStatus gif_write_error(ImageError* error, ImageStatus status, const char* message) {
    if (error) {
        *error = {}; error->code = status; error->format = IMAGE_CODEC_GIF;
        strcpy(error->operation, "encode");
        strncpy(error->message, message, sizeof(error->message) - 1);
    }
    return status;
}

ImageStatus image_encode_gif_frames(ImageContext* context, const RasterBuffer* const* frames,
        const uint16_t* delays, uint32_t count, uint32_t plays, ImageEncoded* encoded, ImageError* error) {
    ImageContext defaults; image_context_init(&defaults);
    if (!context) context = &defaults;
    if (encoded) *encoded = {};
    if (!encoded || !frames || !delays || !count || count > context->limits.max_frames ||
            plays > 65536 || !frames[0] || !frames[0]->width || !frames[0]->height || frames[0]->channels != 4 ||
            frames[0]->width > 65535 || frames[0]->height > 65535)
        return gif_write_error(error, IMAGE_INVALID_ARGUMENT, "invalid GIF frame/loop description");
    uint32_t width = frames[0]->width, height = frames[0]->height;
    size_t size = (size_t)width * height;
    if (size > context->limits.max_source_pixels || size > context->limits.max_scratch_bytes)
        return gif_write_error(error, IMAGE_LIMIT_EXCEEDED, "GIF frame exceeds the scratch/pixel budget");
    GifOutput output = {strbuf_new(), context->limits.max_encoded_bytes};
    uint8_t* indices = (uint8_t*)mem_alloc(size, MEM_CAT_IMAGE);
    if (!output.bytes || !indices) {
        strbuf_free(output.bytes); mem_free(indices);
        return gif_write_error(error, IMAGE_OUT_OF_MEMORY, "cannot allocate GIF output");
    }
    int native_error = 0;
    GifFileType* gif = EGifOpen(&output, gif_write_bytes, &native_error);
    GifColorType colors[256] = {};
    ColorMapObject* global = GifMakeMapObject(256, colors);
    bool valid = gif && global;
    if (gif) EGifSetGifVersion(gif, true);
    if (valid) valid = EGifPutScreenDesc(gif, (int)width, (int)height, 8, 255, global) == GIF_OK;
    GifFreeMapObject(global);
    if (valid && plays != 1) {
        uint16_t loops = plays ? (uint16_t)(plays - 1) : 0;
        uint8_t data[] = {1, (uint8_t)loops, (uint8_t)(loops >> 8)};
        valid = EGifPutExtensionLeader(gif, APPLICATION_EXT_FUNC_CODE) == GIF_OK &&
            EGifPutExtensionBlock(gif, 11, "NETSCAPE2.0") == GIF_OK &&
            EGifPutExtensionBlock(gif, 3, data) == GIF_OK && EGifPutExtensionTrailer(gif) == GIF_OK;
    }
    const char* failure = "GIF encoder failed";
    ImageStatus status = IMAGE_INVALID_DATA;
    for (uint32_t f = 0; f < count && valid; f++) {
        if (context->cancelled && context->cancelled(context->user)) {
            valid = false; failure = "GIF encoding cancelled"; status = IMAGE_CANCELLED; break;
        }
        const RasterBuffer* frame = frames[f];
        if (!frame || frame->channels != 4 || frame->width != width || frame->height != height) {
            valid = false; failure = "GIF frames must share an RGBA8 extent"; status = IMAGE_INVALID_ARGUMENT; break;
        }
        HashMap* palette = hashmap_new(sizeof(GifPaletteEntry), 256, 0, 0,
            gif_palette_hash, gif_palette_compare, nullptr, nullptr);
        if (!palette) { valid = false; status = IMAGE_OUT_OF_MEMORY; break; }
        unsigned color_count = 0;
        memset(colors, 0, sizeof(colors));
        for (size_t p = 0; p < size && valid; p++) {
            const uint8_t* pixel = raster_buffer_pixel(frame, (uint32_t)(p % width), (uint32_t)(p / width));
            if (!pixel[3]) { indices[p] = 255; continue; }
            if (pixel[3] != 255) {
                valid = false; failure = "GIF needs binary alpha; quantize alpha explicitly"; status = IMAGE_UNSUPPORTED_FEATURE; break;
            }
            GifPaletteEntry probe = {(uint32_t)pixel[0] | ((uint32_t)pixel[1] << 8) | ((uint32_t)pixel[2] << 16), 0};
            const GifPaletteEntry* existing = (const GifPaletteEntry*)hashmap_get(palette, &probe);
            if (existing) indices[p] = existing->index;
            else {
                if (color_count == 255) {
                    valid = false; failure = "GIF palette exceeds 255 colors; quantize explicitly"; status = IMAGE_UNSUPPORTED_FEATURE; break;
                }
                probe.index = (uint8_t)color_count;
                colors[color_count++] = {pixel[0], pixel[1], pixel[2]};
                hashmap_set(palette, &probe); indices[p] = probe.index;
                if (hashmap_oom(palette)) { valid = false; status = IMAGE_OUT_OF_MEMORY; }
            }
        }
        hashmap_free(palette);
        if (!valid) break;
        ColorMapObject* local = GifMakeMapObject(256, colors);
        GraphicsControlBlock control = {DISPOSE_BACKGROUND, false, delays[f], 255};
        uint8_t extension[4]; EGifGCBToExtension(&control, extension);
        valid = local && EGifPutExtension(gif, GRAPHICS_EXT_FUNC_CODE, 4, extension) == GIF_OK &&
            EGifPutImageDesc(gif, 0, 0, (int)width, (int)height, false, local) == GIF_OK;
        for (uint32_t y = 0; y < height && valid; y++)
            valid = EGifPutLine(gif, indices + (size_t)y * width, (int)width) == GIF_OK;
        GifFreeMapObject(local);
    }
    if (gif && EGifCloseFile(gif, &native_error) == GIF_ERROR) valid = false;
    if (output.failed) { valid = false; failure = "GIF output exceeds the encoded byte limit"; status = IMAGE_LIMIT_EXCEEDED; }
    if (valid) {
        // strbuf uses libc allocation; the image API requires mem_alloc/image_free ownership.
        encoded->data = (uint8_t*)mem_alloc(output.bytes->length, MEM_CAT_IMAGE);
        if (encoded->data) {
            memcpy(encoded->data, output.bytes->str, output.bytes->length);
            encoded->length = output.bytes->length;
        } else { valid = false; status = IMAGE_OUT_OF_MEMORY; failure = "cannot allocate GIF bytes"; }
    }
    strbuf_free(output.bytes); mem_free(indices);
    return valid ? IMAGE_OK : gif_write_error(error, status, failure);
}
