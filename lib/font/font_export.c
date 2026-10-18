#include "font_export.h"
#include "font_internal.h"
#include "../endian.h"
#include "../hashmap.h"
#include "../mem.h"
#include <string.h>

static uint32_t font_export_checksum(const uint8_t* bytes, size_t length) {
    uint32_t sum = 0;
    for (size_t offset = 0; offset < length; offset += 4) {
        uint32_t word = 0;
        for (size_t byte = 0; byte < 4; byte++)
            word = (word << 8) | (offset + byte < length ? bytes[offset + byte] : 0);
        sum += word;
    }
    return sum;
}

void font_export_free(FontExport* result) {
    if (result) { mem_free(result->bytes); memset(result, 0, sizeof(*result)); }
}

bool font_export_face(FontHandle* font, FontExport* result, const char** error) {
    if (!result || !error) return false;
    memset(result, 0, sizeof(*result));
    *error = "selected font has no exportable SFNT face";
    FontTables* tables = font && !font->resources_destroyed ? font->tables : NULL;
    if (!tables || !tables->num_tables) return false;
    HeadTable* head = font_tables_get_head(tables);
    HheaTable* hhea = font_tables_get_hhea(tables);
    Os2Table* os2 = font_tables_get_os2(tables);
    PostTable* post = font_tables_get_post(tables);
    NameTable* name = font_tables_get_name(tables);
    if (!head || !head->units_per_em || !hhea || !name || !name->postscript_name) return false;
    // OS/2 permissions apply to the selected face, including TTC/WOFF sources.
    if (os2 && (os2->fs_type & 2)) {
        *error = "selected font forbids outline font embedding"; return false;
    }
    bool cff = font_tables_has(tables, FONT_TAG('C','F','F',' '));
    bool generated = (!cff && !font_tables_has(tables, FONT_TAG('g','l','y','f'))) ||
        font_tables_has(tables, FONT_TAG('f','v','a','r')) ||
        font_tables_has(tables, FONT_TAG('s','b','i','x')) ||
        font_tables_has(tables, FONT_TAG('C','B','D','T')) ||
        font_tables_has(tables, FONT_TAG('C','O','L','R')) ||
        (os2 && os2->version >= 2 && (os2->fs_type & 0x200));
    if (generated && os2 && os2->version >= 2 && (os2->fs_type & 0x100)) {
        *error = "selected native font forbids glyph-program subsetting"; return false;
    }
    uint16_t table_count = 0;
    for (uint16_t i = 0; i < tables->num_tables; i++)
        if (tables->dirs[i].tag != FONT_TAG('D','S','I','G')) table_count++;
    size_t length = 12 + (size_t)table_count * 16;
    for (uint16_t i = 0; i < tables->num_tables; i++) {
        FontTableDir* table = &tables->dirs[i];
        size_t aligned = ((size_t)table->length + 3) & ~(size_t)3;
        if (!font_data_range_valid(tables->data_len, table->offset, table->length) ||
                aligned > UINT32_MAX || length > UINT32_MAX - aligned) {
            *error = "selected font table extent is invalid"; return false;
        }
        if (table->tag != FONT_TAG('D','S','I','G')) length += aligned;
    }
    uint8_t* bytes = generated ? NULL : (uint8_t*)mem_calloc(1, length, MEM_CAT_FONT);
    if (!generated) {
        if (!bytes) { *error = "cannot allocate standalone font program"; return false; }
        write_be32(bytes, tables->scaler_type);
        write_be16(bytes + 4, table_count);
        uint16_t power = 1, selector = 0;
        while (power <= table_count / 2) { power *= 2; selector++; }
        write_be16(bytes + 6, power * 16); write_be16(bytes + 8, selector);
        write_be16(bytes + 10, table_count * 16 - power * 16);
        size_t offset = 12 + (size_t)table_count * 16, head_offset = 0;
        uint16_t directory_index = 0;
        // rebuild offsets: TTC tables address the collection, not a standalone face.
        for (uint16_t i = 0; i < tables->num_tables; i++) {
            FontTableDir* table = &tables->dirs[i];
            // normalization invalidates signatures over the source font/collection.
            if (table->tag == FONT_TAG('D','S','I','G')) continue;
            uint8_t* record = bytes + 12 + (size_t)directory_index++ * 16;
            memcpy(bytes + offset, tables->data + table->offset, table->length);
            if (table->tag == FONT_TAG('h','e','a','d') && table->length >= 12) {
                head_offset = offset; write_be32(bytes + offset + 8, 0);
            }
            write_be32(record, table->tag);
            write_be32(record + 4, font_export_checksum(bytes + offset, table->length));
            write_be32(record + 8, (uint32_t)offset); write_be32(record + 12, table->length);
            offset += ((size_t)table->length + 3) & ~(size_t)3;
        }
        if (!head_offset) { mem_free(bytes); return false; }
        write_be32(bytes + head_offset + 8, 0xB1B0AFBAu - font_export_checksum(bytes, length));
        result->bytes = bytes; result->length = length;
        result->fingerprint = hashmap_sip(bytes, length, 0, 0);
    } else {
        // retain native instance identity: variable/optical glyph programs can differ by size.
        result->fingerprint = hashmap_sip(tables->dirs, (size_t)tables->num_tables * sizeof(FontTableDir),
            (uint64_t)(uintptr_t)font, font->cache_identity);
    }
    result->postscript_name = name->postscript_name;
    result->units_per_em = head->units_per_em;
    float scale = 1000.0f / head->units_per_em;
    result->bbox[0] = head->x_min * scale; result->bbox[1] = head->y_min * scale;
    result->bbox[2] = head->x_max * scale; result->bbox[3] = head->y_max * scale;
    result->ascent = hhea->ascender * scale; result->descent = hhea->descender * scale;
    result->cap_height = os2 && os2->s_cap_height > 0 ? os2->s_cap_height * scale : result->ascent;
    uint32_t post_length = 0;
    const uint8_t* raw_post = font_tables_find(tables, FONT_TAG('p','o','s','t'), &post_length);
    result->italic_angle = raw_post && post_length >= 8 ? (float)read_be32s(raw_post + 4) / 65536.0f : 0.0f;
    result->cff = cff; result->monospace = post && post->is_fixed_pitch;
    result->glyph_program = generated;
    result->bitmap_only = os2 && os2->version >= 2 && (os2->fs_type & 0x200);
    *error = NULL;
    return true;
}

float font_export_glyph_width(FontHandle* font, uint32_t glyph) {
    FontTables* tables = font && !font->resources_destroyed ? font->tables : NULL;
    HeadTable* head = tables ? font_tables_get_head(tables) : NULL;
    MaxpTable* maxp = tables ? font_tables_get_maxp(tables) : NULL;
    HmtxTable* metrics = tables ? font_tables_get_hmtx(tables) : NULL;
    return head && head->units_per_em && maxp && glyph < maxp->num_glyphs && metrics
        ? 1000.0f * hmtx_get_advance(metrics, (uint16_t)glyph) / head->units_per_em : -1.0f;
}

float font_export_italic_shear(FontHandle* font) {
    if (!font || font->resources_destroyed || font->slant == FONT_SLANT_NORMAL) return 0;
    HeadTable* head = font->tables ? font_tables_get_head(font->tables) : NULL;
    return head && !(head->mac_style & 2) ? FONT_SYNTHETIC_ITALIC_SHEAR : 0;
}
