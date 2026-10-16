#pragma once
#include "raster.h"
#include "arraylist.h"
#include "mempool.h"
#include "strbuf.h"

typedef struct PsdChannel {
    int16_t id;
    uint32_t length;
} PsdChannel;

typedef struct PsdLayer {
    int32_t left, top, right, bottom;
    int32_t mask_left, mask_top, mask_right, mask_bottom;
    uint32_t id, section;
    uint8_t opacity, flags, clipping, mask_flags, mask_default;
    char blend[5];
    char* name;
    size_t name_length;
    uint16_t channel_count;
    PsdChannel channels[56];
    RasterBuffer* pixels;
    RasterBuffer* mask;
    bool unsupported;
} PsdLayer;

typedef struct PsdDocument {
    Pool* pool;
    ArrayList* layers; // PSD record order, bottom to top, including section dividers.
    uint32_t width, height;
    uint16_t mode, depth, channels;
    RasterBuffer* composite;
    const uint8_t* profile;
    size_t profile_length;
    size_t decoded_bytes;
    bool owns_rasters;
    bool merged_transparency;
    bool unsupported;
} PsdDocument;

PsdDocument* psd_document_create(void);
void psd_document_destroy(PsdDocument* document);
bool psd_read(PsdDocument* document, const uint8_t* bytes, size_t length, const char** error);
bool psd_write(const PsdDocument* document, StrBuf* output, const char** error);
