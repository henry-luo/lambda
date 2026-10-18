#include "image/image_internal.h"
#include "file.h"
#include "log.h"
#include <gif_lib.h>
#include <stdio.h>
#include <limits.h>

#define IMAGE_MAX_DECODED_BYTES ((size_t)512 * 1024 * 1024)
#define IMAGE_MAX_ENCODED_BYTES ((size_t)128 * 1024 * 1024)
#define IMAGE_MAX_GIF_FRAMES 4096

static bool image_rgba_layout(int width, int height,
                              size_t* pixel_count, size_t* byte_count) {
    size_t pixels = 0;
    size_t bytes = 0;
    if (width <= 0 || height <= 0 || width > INT_MAX / 4 ||
        !math_checked_mul((size_t)width, (size_t)height, &pixels) ||
        !math_checked_mul(pixels, 4, &bytes) || bytes > IMAGE_MAX_DECODED_BYTES) {
        return false;
    }
    if (pixel_count) *pixel_count = pixels;
    if (byte_count) *byte_count = bytes;
    return true;
}

static bool image_data_range_valid(size_t total, size_t offset, size_t length) {
    return offset <= total && length <= total - offset;
}

static bool gif_skip_sub_blocks(const unsigned char* data, size_t length,
                                size_t* position) {
    while (*position < length) {
        size_t block_length = data[(*position)++];
        if (block_length == 0) return true;
        if (!image_data_range_valid(length, *position, block_length)) return false;
        *position += block_length;
    }
    return false;
}

// Validate the complete container before giflib expands any raster data.  The
// estimate includes giflib's indexed frames and our retained RGBA canvases.
static bool gif_preflight(const unsigned char* data, size_t length,
                          int* out_frame_count) {
    if (out_frame_count) *out_frame_count = 0;
    if (!data || length < 14 || length > IMAGE_MAX_ENCODED_BYTES ||
        (memcmp(data, "GIF87a", 6) != 0 && memcmp(data, "GIF89a", 6) != 0)) {
        return false;
    }

    int screen_width = read_le16(data + 6);
    int screen_height = read_le16(data + 8);
    size_t screen_bytes = 0;
    if (!image_rgba_layout(screen_width, screen_height, NULL, &screen_bytes)) return false;

    size_t position = 13;
    unsigned char screen_flags = data[10];
    if (screen_flags & 0x80) {
        size_t color_bytes = (size_t)3 << ((screen_flags & 0x07) + 1);
        if (!image_data_range_valid(length, position, color_bytes)) return false;
        position += color_bytes;
    }

    int frame_count = 0;
    size_t raster_bytes = 0;
    bool saw_trailer = false;
    while (position < length) {
        unsigned char marker = data[position++];
        if (marker == 0x3b) {
            saw_trailer = true;
            break;
        }
        if (marker == 0x21) {
            if (position >= length) return false;
            position++;  // extension label
            if (!gif_skip_sub_blocks(data, length, &position)) return false;
            continue;
        }
        if (marker != 0x2c || !image_data_range_valid(length, position, 9)) return false;

        int frame_width = read_le16(data + position + 4);
        int frame_height = read_le16(data + position + 6);
        unsigned char frame_flags = data[position + 8];
        position += 9;
        size_t frame_pixels = 0;
        if (frame_width <= 0 || frame_height <= 0 ||
            !math_checked_mul((size_t)frame_width, (size_t)frame_height,
                              &frame_pixels) ||
            !math_checked_add(raster_bytes, frame_pixels, &raster_bytes) ||
            raster_bytes > IMAGE_MAX_DECODED_BYTES ||
            ++frame_count > IMAGE_MAX_GIF_FRAMES) {
            return false;
        }
        if (frame_flags & 0x80) {
            size_t color_bytes = (size_t)3 << ((frame_flags & 0x07) + 1);
            if (!image_data_range_valid(length, position, color_bytes)) return false;
            position += color_bytes;
        }
        if (position >= length) return false;
        position++;  // LZW minimum code size
        if (!gif_skip_sub_blocks(data, length, &position)) return false;
    }

    size_t retained_buffers = 0;
    size_t retained_bytes = 0;
    size_t peak_bytes = 0;
    if (!saw_trailer || frame_count == 0 ||
        !math_checked_add((size_t)frame_count, 2, &retained_buffers) ||
        !math_checked_mul(screen_bytes, retained_buffers, &retained_bytes) ||
        !math_checked_add(retained_bytes, raster_bytes, &peak_bytes) ||
        peak_bytes > IMAGE_MAX_DECODED_BYTES) {
        return false;
    }
    if (out_frame_count) *out_frame_count = frame_count;
    return true;
}

typedef struct GifMemoryReader {
    const unsigned char* data;
    size_t size;
    size_t pos;
    const ImageSource* source;
    ImageOperation* op;
} GifMemoryReader;

static int gif_memory_read_func(GifFileType* gif, GifByteType* buf, int size) {
    GifMemoryReader* reader = (GifMemoryReader*)gif->UserData;
    if (size < 0) return 0;
    size_t avail = reader->size - reader->pos;
    size_t to_read = (size_t)size;
    if (to_read > avail) to_read = avail;
    if (to_read > 0) {
        if (reader->source) {
            if (!image_source_read(reader->op, reader->source, reader->pos, buf, to_read)) return 0;
        } else memcpy(buf, reader->data + reader->pos, to_read);
        reader->pos += to_read;
    }
    return (int)to_read;
}

// Composite a single GIF frame onto a canvas, respecting frame position and transparency.
// canvas must be width*height*4 bytes (RGBA).
static void gif_composite_frame(uint32_t* canvas, int canvas_w, int canvas_h,
                                 GifFileType* gif, int frame_idx) {
    SavedImage* frame = &gif->SavedImages[frame_idx];
    GifImageDesc* desc = &frame->ImageDesc;
    ColorMapObject* cmap = desc->ColorMap ? desc->ColorMap : gif->SColorMap;
    if (!cmap) return;

    // Extract transparency from Graphics Control Block
    GraphicsControlBlock gcb;
    int transparent_color = -1;
    if (DGifSavedExtensionToGCB(gif, frame_idx, &gcb) == GIF_OK) {
        transparent_color = gcb.TransparentColor;
    }

    GifByteType* raster = frame->RasterBits;
    int fw = desc->Width, fh = desc->Height;
    int left = desc->Left, top = desc->Top;

    for (int y = 0; y < fh; y++) {
        int dy = top + y;
        if (dy < 0 || dy >= canvas_h) continue;
        for (int x = 0; x < fw; x++) {
            int dx = left + x;
            if (dx < 0 || dx >= canvas_w) continue;
            int ci = raster[(size_t)y * (size_t)fw + (size_t)x];
            if (ci == transparent_color) continue;
            if (ci >= cmap->ColorCount) continue;
            GifColorType* c = &cmap->Colors[ci];
            uint32_t pixel = ((uint32_t)c->Red) | ((uint32_t)c->Green << 8) |
                             ((uint32_t)c->Blue << 16) | (0xFFu << 24);
            canvas[(size_t)dy * (size_t)canvas_w + (size_t)dx] = pixel;
        }
    }
}

static bool gif_find_loop_extension(const ExtensionBlock* blocks, int count, int* loops) {
    for (int i = 0; i + 1 < count; i++) {
        const ExtensionBlock* ext = &blocks[i];
        const ExtensionBlock* sub = &blocks[i + 1];
        if (ext->Function == APPLICATION_EXT_FUNC_CODE && ext->ByteCount >= 11 &&
                (!memcmp(ext->Bytes, "NETSCAPE2.0", 11) || !memcmp(ext->Bytes, "ANIMEXTS1.0", 11)) &&
                sub->ByteCount >= 3 && sub->Bytes[0] == 1) {
            *loops = sub->Bytes[1] | (sub->Bytes[2] << 8);
            return true;
        }
    }
    return false;
}

// Internal: decode all frames from an already-slurped GifFileType.
// Returns NULL if < 2 frames. Caller must free with image_gif_free().
static GifFrames* gif_decode_all_frames(GifFileType* gif) {
    if (!gif || gif->ImageCount < 2) return NULL;

    int w = gif->SWidth;
    int h = gif->SHeight;
    int n = gif->ImageCount;

    size_t pixel_count = 0;
    size_t canvas_bytes = 0;
    size_t retained_bytes = 0;
    size_t retained_buffers = 0;
    if (n <= 1 || n > IMAGE_MAX_GIF_FRAMES ||
        !image_rgba_layout(w, h, &pixel_count, &canvas_bytes) ||
        !math_checked_add((size_t)n, 2, &retained_buffers) ||
        !math_checked_mul(canvas_bytes, retained_buffers, &retained_bytes) ||
        retained_bytes > IMAGE_MAX_DECODED_BYTES) {
        log_error("[image] rejected animated GIF dimensions/frames %dx%d x %d", w, h, n);
        return NULL;
    }

    GifFrames* result = (GifFrames*)mem_calloc(1, sizeof(GifFrames), MEM_CAT_IMAGE);
    if (!result) return NULL;
    result->width = w;
    result->height = h;
    result->loop_count = 0;  // default: infinite

    // giflib associates pre-image extensions with SavedImages, not the file tail.
    result->has_loop_extension = gif_find_loop_extension(gif->ExtensionBlocks,
        gif->ExtensionBlockCount, &result->loop_count);
    for (int i = 0; i < n && !result->has_loop_extension; i++)
        result->has_loop_extension = gif_find_loop_extension(gif->SavedImages[i].ExtensionBlocks,
            gif->SavedImages[i].ExtensionBlockCount, &result->loop_count);

    result->frames = (GifFrameData*)mem_calloc((size_t)n, sizeof(GifFrameData), MEM_CAT_IMAGE);
    if (!result->frames) {
        mem_free(result);
        return NULL;
    }
    result->frame_count = n;

    // Background color for disposal
    uint32_t bg_pixel = 0;  // transparent black
    if (gif->SColorMap && gif->SBackGroundColor < gif->SColorMap->ColorCount) {
        GifColorType* bgc = &gif->SColorMap->Colors[gif->SBackGroundColor];
        bg_pixel = ((uint32_t)bgc->Red) | ((uint32_t)bgc->Green << 8) |
                   ((uint32_t)bgc->Blue << 16) | (0xFFu << 24);
    }

    // Canvas for progressive compositing
    uint32_t* canvas = (uint32_t*)mem_calloc(pixel_count, sizeof(uint32_t), MEM_CAT_IMAGE);
    if (!canvas) {
        image_gif_free(result);
        return NULL;
    }
    uint32_t* prev_canvas = NULL;  // for disposal method 3 (restore previous)

    for (int i = 0; i < n; i++) {
        // Get graphics control block for this frame
        GraphicsControlBlock gcb;
        int disposal = DISPOSAL_UNSPECIFIED;
        int delay_ms = 100;  // default 100ms (GIF spec: 10cs = 100ms)
        if (DGifSavedExtensionToGCB(gif, i, &gcb) == GIF_OK) {
            disposal = gcb.DisposalMode;
            delay_ms = gcb.DelayTime * 10;  // convert centiseconds to ms
            if (delay_ms <= 0) delay_ms = 100;  // browsers use 100ms for 0-delay
        }

        // Save canvas before compositing for disposal method 3
        if (disposal == DISPOSE_PREVIOUS) {
            if (!prev_canvas) {
                prev_canvas = (uint32_t*)mem_calloc(pixel_count, sizeof(uint32_t), MEM_CAT_IMAGE);
                if (!prev_canvas) {
                    mem_free(canvas);
                    image_gif_free(result);
                    return NULL;
                }
            }
            memcpy(prev_canvas, canvas, canvas_bytes);
        }

        // Composite this frame onto canvas
        gif_composite_frame(canvas, w, h, gif, i);

        // Snapshot the composited canvas as this frame's pixels
        result->frames[i].pixels = (uint32_t*)mem_calloc(pixel_count, sizeof(uint32_t), MEM_CAT_IMAGE);
        if (!result->frames[i].pixels) {
            mem_free(canvas);
            if (prev_canvas) mem_free(prev_canvas);
            image_gif_free(result);
            return NULL;
        }
        memcpy(result->frames[i].pixels, canvas, canvas_bytes);
        result->frames[i].delay_ms = delay_ms;
        result->frames[i].source_delay_ms = DGifSavedExtensionToGCB(gif, i, &gcb) == GIF_OK ? gcb.DelayTime * 10 : 0;
        result->frames[i].disposal = disposal;

        // Apply disposal for next frame
        switch (disposal) {
            case DISPOSE_BACKGROUND: {
                // a background palette index marked transparent clears to transparent.
                uint32_t clear = gcb.TransparentColor != NO_TRANSPARENT_COLOR &&
                    gcb.TransparentColor == gif->SBackGroundColor ? 0 : bg_pixel;
                SavedImage* frame = &gif->SavedImages[i];
                GifImageDesc* desc = &frame->ImageDesc;
                for (int y = desc->Top; y < desc->Top + desc->Height && y < h; y++) {
                    for (int x = desc->Left; x < desc->Left + desc->Width && x < w; x++) {
                        canvas[(size_t)y * (size_t)w + (size_t)x] = clear;
                    }
                }
                break;
            }
            case DISPOSE_PREVIOUS:
                // Restore canvas to state before this frame
                if (prev_canvas) {
                    memcpy(canvas, prev_canvas, canvas_bytes);
                }
                break;
            default:
                // DISPOSAL_UNSPECIFIED / DISPOSE_DO_NOT: leave canvas as-is
                break;
        }
    }

    mem_free(canvas);
    if (prev_canvas) mem_free(prev_canvas);

    return result;
}

GifFrames* image_gif_load_from_memory(const unsigned char* data, size_t length) {
    if (!gif_preflight(data, length, NULL)) return NULL;

    GifMemoryReader reader = { data, length, 0 };
    int error_code;
    GifFileType* gif = DGifOpen(&reader, gif_memory_read_func, &error_code);
    if (!gif) {
        log_error("gif multi-frame: failed to open from memory (error %d)", error_code);
        return NULL;
    }
    if (DGifSlurp(gif) == GIF_ERROR) {
        log_error("gif multi-frame: failed to slurp from memory (error %d)", gif->Error);
        DGifCloseFile(gif, &error_code);
        return NULL;
    }
    GifFrames* result = gif_decode_all_frames(gif);
    DGifCloseFile(gif, &error_code);
    return result;
}

void image_gif_free(GifFrames* gif) {
    if (!gif) return;
    if (gif->frames) {
        for (int i = 0; i < gif->frame_count; i++) {
            if (gif->frames[i].pixels) mem_free(gif->frames[i].pixels);
        }
        mem_free(gif->frames);
    }
    mem_free(gif);
}

int image_gif_frame_count_from_memory(const unsigned char* data, size_t length) {
    if (!gif_preflight(data, length, NULL)) return 0;
    GifMemoryReader reader = { data, length, 0 };
    int error_code;
    GifFileType* gif = DGifOpen(&reader, gif_memory_read_func, &error_code);
    if (!gif) return 0;
    if (DGifSlurp(gif) == GIF_ERROR) {
        DGifCloseFile(gif, &error_code);
        return 0;
    }
    int count = gif->ImageCount;
    DGifCloseFile(gif, &error_code);
    return count;
}

ImageStatus image_gif_probe(ImageOperation* op, const ImageSource* source, ImageInfo* info) {
    uint8_t header[13];
    if (!image_source_read(op,source,0,header,sizeof(header))) return op->error->code;
    info->width=read_le16(header+6);info->height=read_le16(header+8);info->bit_depth=8;
    info->has_alpha=true;info->orientation=1;info->page_count=1;info->pages_known=true;
    // counting frames is an optional container scan; a dimension probe stops at the header.
    info->frames_known=false;
    return IMAGE_OK;
}

ImageStatus image_gif_decode(ImageOperation* op,const ImageSource* source,
    const ImageDecodeOptions* options,ImageDecoded* result) {
    if (options->frame_index) {
        const uint8_t* data=image_source_bytes(op,source);if(!data)return op->error->code;
        int count=0;
        if(!gif_preflight(data,source->length,&count))return image_fail(op,IMAGE_INVALID_DATA,"invalid GIF animation");
        if((uint32_t)count>op->context->limits.max_frames ||
            (uint64_t)result->info.width*result->info.height*4*((uint64_t)count+2)>op->context->limits.max_decoded_bytes)
            return image_fail(op,IMAGE_LIMIT_EXCEEDED,"GIF animation exceeds frame/pixel limits");
        if(options->frame_index>=(uint32_t)count)return image_fail(op,IMAGE_INVALID_ARGUMENT,"GIF frame index out of range");
        uint32_t tw,th;image_target_size(&result->info,options,&tw,&th);
        if(options->mode==IMAGE_DECODE_THUMBNAIL&&!options->allow_full_decode&&(tw<result->info.width||th<result->info.height))
            return image_fail(op,IMAGE_FAST_PATH_UNAVAILABLE,"later GIF frames need compositing canvases");
        GifFrames* frames=image_gif_load_from_memory(data,source->length);
        if(!frames)return image_fail(op,IMAGE_INVALID_DATA,"GIF frame compositing failed");
        if(image_allocate_pixels(op,(uint32_t)frames->width,(uint32_t)frames->height,result))
            memcpy(result->pixels,frames->frames[options->frame_index].pixels,result->size);
        result->delay_ms=(uint32_t)frames->frames[options->frame_index].delay_ms;
        result->source_delay_ms=(uint32_t)frames->frames[options->frame_index].source_delay_ms;
        result->has_loop_extension=frames->has_loop_extension;
        result->loop_count=(uint32_t)frames->loop_count;
        image_gif_free(frames);return op->error->code;
    }
    GifMemoryReader reader={NULL,source->length,0,source,op};int native_error=0;
    GifFileType* gif=DGifOpen(&reader,gif_memory_read_func,&native_error);
    if(!gif)return image_fail(op,IMAGE_INVALID_DATA,"GIF open error %d",native_error);
    int transparent=-1;GifRecordType record=UNDEFINED_RECORD_TYPE;bool found=false;
    result->delay_ms=100;
    while(DGifGetRecordType(gif,&record)==GIF_OK){
        if(record==IMAGE_DESC_RECORD_TYPE){found=true;break;}
        if(record==TERMINATE_RECORD_TYPE)break;
        if(record==EXTENSION_RECORD_TYPE){int code=0;GifByteType* extension=NULL;
            if(DGifGetExtension(gif,&code,&extension)==GIF_ERROR)break;
            if(code==GRAPHICS_EXT_FUNC_CODE&&extension&&extension[0]>=4){
                if(extension[1]&1)transparent=extension[4];
                unsigned delay=read_le16(extension+2)*10;result->delay_ms=delay?delay:100;
                result->source_delay_ms=delay;
            }
            bool loop_extension=code==APPLICATION_EXT_FUNC_CODE&&extension&&extension[0]>=11&&
                !memcmp(extension+1,"NETSCAPE2.0",11);
            if(loop_extension&&DGifGetExtensionNext(gif,&extension)==GIF_OK&&extension&&extension[0]>=3&&extension[1]==1) {
                result->loop_count=read_le16(extension+2);
                result->has_loop_extension=true;
            }
            while(extension)if(DGifGetExtensionNext(gif,&extension)==GIF_ERROR)break;
        }
        if(image_op_cancelled(op))break;
    }
    if(!found||DGifGetImageDesc(gif)==GIF_ERROR){image_fail(op,IMAGE_INVALID_DATA,"GIF has no valid display frame");DGifCloseFile(gif,&native_error);return op->error->code;}
    GifImageDesc* desc=&gif->Image;
    if(desc->Width<=0||desc->Height<=0||!image_check_dimensions(op,(uint32_t)desc->Width,(uint32_t)desc->Height)){
        image_fail(op,IMAGE_INVALID_DATA,"invalid GIF frame dimensions");DGifCloseFile(gif,&native_error);return op->error->code;}
    uint32_t tw,th;image_target_size(&result->info,options,&tw,&th);
    bool shrinking=tw<result->info.width||th<result->info.height;
    if(desc->Interlace&&shrinking&&!options->allow_full_decode){DGifCloseFile(gif,&native_error);return image_fail(op,IMAGE_FAST_PATH_UNAVAILABLE,"interlaced GIF requires an indexed frame buffer");}
    uint8_t* indices=(uint8_t*)image_op_alloc(op,(size_t)desc->Width*(desc->Interlace?(size_t)desc->Height:1),false);
    uint8_t* row=(uint8_t*)image_op_alloc(op,(size_t)result->info.width*4,false);
    ImageReducer reducer;
    if(!indices||!row||!image_reducer_init(&reducer,op,result->info.width,result->info.height,tw,th,result)){
        DGifCloseFile(gif,&native_error);return op->error->code;}
    if(desc->Interlace){static const int start[4]={0,4,2,1},step[4]={8,8,4,2};
        for(unsigned pass=0;pass<4;pass++)for(int y=start[pass];y<desc->Height;y+=step[pass])
            if(DGifGetLine(gif,indices+(size_t)y*desc->Width,desc->Width)==GIF_ERROR)image_fail(op,IMAGE_INVALID_DATA,"invalid interlaced GIF raster");}
    ColorMapObject* palette=desc->ColorMap?desc->ColorMap:gif->SColorMap;
    for(uint32_t y=0;y<result->info.height&&op->error->code==IMAGE_OK;y++){
        memset(row,0,(size_t)result->info.width*4);
        if(y>=(uint32_t)desc->Top&&y-(uint32_t)desc->Top<(uint32_t)desc->Height){
            uint8_t* line=desc->Interlace?indices+(size_t)(y-desc->Top)*desc->Width:indices;
            if(!desc->Interlace&&DGifGetLine(gif,line,desc->Width)==GIF_ERROR){image_fail(op,IMAGE_INVALID_DATA,"invalid GIF raster");break;}
            for(int x=0;x<desc->Width;x++){
                uint32_t dx=(uint32_t)desc->Left+(uint32_t)x;unsigned index=line[x];
                if(dx>=result->info.width||index==(unsigned)transparent)continue;
                if(!palette||index>=(unsigned)palette->ColorCount){image_fail(op,IMAGE_INVALID_DATA,"GIF palette index out of range");break;}
                GifColorType* color=&palette->Colors[index];uint8_t* p=row+dx*4;
                p[0]=color->Red;p[1]=color->Green;p[2]=color->Blue;p[3]=255;
            }
        }
        image_reducer_row(&reducer,row,y);
    }
    image_reducer_finish(&reducer);
    if(shrinking)result->strategy=desc->Interlace?IMAGE_STRATEGY_FULL_RESIZE:IMAGE_STRATEGY_STREAMED_REDUCE;
    result->full_decode_fallback=shrinking&&desc->Interlace;
    DGifCloseFile(gif,&native_error);return op->error->code;
}
