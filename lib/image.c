#include "image/image_internal.h"

static uint16_t read_exif_u16(const unsigned char* p, bool little_endian) {
    return little_endian ? read_le16((const uint8_t*)p) : read_be16((const uint8_t*)p);
}

static uint32_t read_exif_u32(const unsigned char* p, bool little_endian) {
    return little_endian ? read_le32((const uint8_t*)p) : read_be32((const uint8_t*)p);
}

int image_jpeg_exif_orientation_from_memory(const unsigned char* data, size_t size) {
    if (!data || size < 4 || data[0] != 0xFF || data[1] != 0xD8) return 1;

    size_t pos = 2;
    while (pos + 4 <= size) {
        while (pos < size && data[pos] == 0xFF) pos++;
        if (pos >= size) break;

        unsigned char marker = data[pos++];
        if (marker == 0xDA || marker == 0xD9) break;
        if (pos + 2 > size) break;

        uint16_t seg_len = (uint16_t)((data[pos] << 8) | data[pos + 1]);
        pos += 2;
        if (seg_len < 2) break;

        size_t payload_len = (size_t)seg_len - 2;
        if (pos + payload_len > size) break;

        if (marker == 0xE1 && payload_len >= 14 && memcmp(data + pos, "Exif\0\0", 6) == 0) {
            const unsigned char* tiff = data + pos + 6;
            size_t tiff_len = payload_len - 6;
            if (tiff_len < 8) return 1;

            bool little_endian = false;
            if (tiff[0] == 'I' && tiff[1] == 'I') little_endian = true;
            else if (tiff[0] == 'M' && tiff[1] == 'M') little_endian = false;
            else return 1;

            if (read_exif_u16(tiff + 2, little_endian) != 42) return 1;
            uint32_t ifd_offset = read_exif_u32(tiff + 4, little_endian);
            // widen before addition: hostile offsets must not wrap into the segment.
            if ((uint64_t)ifd_offset + 2 > tiff_len) return 1;

            const unsigned char* ifd = tiff + ifd_offset;
            uint16_t entry_count = read_exif_u16(ifd, little_endian);
            size_t entries_start = ifd_offset + 2;
            for (uint16_t i = 0; i < entry_count; i++) {
                size_t entry_offset = entries_start + (size_t)i * 12;
                if (entry_offset + 12 > tiff_len) break;

                const unsigned char* entry = tiff + entry_offset;
                uint16_t tag = read_exif_u16(entry, little_endian);
                uint16_t type = read_exif_u16(entry + 2, little_endian);
                uint32_t count = read_exif_u32(entry + 4, little_endian);
                if (tag == 0x0112 && type == 3 && count >= 1) {
                    int orientation = read_exif_u16(entry + 8, little_endian);
                    return (orientation >= 1 && orientation <= 8) ? orientation : 1;
                }
            }
            return 1;
        }
        pos += payload_len;
    }
    return 1;
}

// legacy byte entry points share the same registry and decode implementation.
static unsigned char* image_load_memory_compat(const unsigned char* data, size_t length,
    int target_w, int target_h, int* width, int* height, int* channels) {
    if (!width || !height || !channels || target_w < 0 || target_h < 0) return NULL;
    ImageSource source = image_source_memory(data, length);
    ImageDecoded result = {};
    ImageDecodeOptions options;
    image_decode_options_init(&options);
    options.normalize_orientation = false;
    options.mode = target_w || target_h ? IMAGE_DECODE_AT_LEAST : IMAGE_DECODE_FULL;
    options.target_width = (uint32_t)target_w;
    options.target_height = (uint32_t)target_h;
    if (image_decode(NULL, &source, &options, &result, NULL) != IMAGE_OK) return NULL;
    *width = (int)result.width; *height = (int)result.height; *channels = 4;
    return result.pixels;
}
unsigned char* image_load_from_memory(const unsigned char* data, size_t length,
    int* width, int* height, int* channels) {
    return image_load_memory_compat(data,length,0,0,width,height,channels);
}
unsigned char* image_load_from_memory_scaled(const unsigned char* data, size_t length,
    int target_w, int target_h, int* width, int* height, int* channels) {
    return image_load_memory_compat(data,length,target_w,target_h,width,height,channels);
}
int image_get_dimensions_from_memory(const unsigned char* data, size_t length, int* width, int* height) {
    if (!width || !height) return 0;
    ImageSource source = image_source_memory(data,length); ImageInfo info = {};
    if (image_probe(NULL,&source,&info,NULL) != IMAGE_OK) return 0;
    *width = (int)info.width; *height = (int)info.height; return 1;
}
const char* image_mime_type_from_memory(const unsigned char* data, size_t length) {
    return image_codec_capabilities(image_detect(data,length))->mime;
}
void image_free(unsigned char* pixels) { if (pixels) mem_free(pixels); }
