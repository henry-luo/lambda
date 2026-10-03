/**
 * Lambda Unified Font Module — glyf Table Parser
 *
 * Parses TrueType glyph outlines from glyf + loca tables.
 * Produces GlyphOutline structs containing contours of on/off-curve
 * points, suitable for conversion to cubic Bézier paths.
 *
 * Handles:
 * - Simple glyphs: contour endpoints, flags, delta-encoded coordinates
 * - Compound glyphs: recursive component references with affine transforms
 * - Implicit on-curve midpoints between consecutive off-curve points
 *
 * Copyright (c) 2026 Lambda Script Project
 */

#include "font_glyf.h"
#include "font_tables.h"
#include "../arena.h"
#include "../log.h"

#include <string.h>
#include <math.h>

// ============================================================================
// Byte reading helpers (big-endian, matching font_tables.c)
// ============================================================================

// big-endian table reads — centralized in lib/endian.h
#include "../endian.h"
#define rd16  read_be16
#define rd16s read_be16s
#define rd32  read_be32

// ============================================================================
// glyf simple glyph flag bits
// ============================================================================

#define GLYF_FLAG_ON_CURVE      (1u << 0)
#define GLYF_FLAG_X_SHORT       (1u << 1)
#define GLYF_FLAG_Y_SHORT       (1u << 2)
#define GLYF_FLAG_REPEAT        (1u << 3)
#define GLYF_FLAG_X_SAME        (1u << 4)   // if X_SHORT=0: same as previous; if X_SHORT=1: positive
#define GLYF_FLAG_Y_SAME        (1u << 5)   // if Y_SHORT=0: same as previous; if Y_SHORT=1: positive

// ============================================================================
// Compound glyph flag bits
// ============================================================================

#define COMP_FLAG_ARG_1_AND_2_ARE_WORDS   (1u << 0)
#define COMP_FLAG_ARGS_ARE_XY_VALUES      (1u << 1)
#define COMP_FLAG_WE_HAVE_A_SCALE         (1u << 3)
#define COMP_FLAG_MORE_COMPONENTS         (1u << 5)
#define COMP_FLAG_WE_HAVE_AN_X_AND_Y_SCALE (1u << 6)
#define COMP_FLAG_WE_HAVE_A_TWO_BY_TWO    (1u << 7)

// maximum compound glyph recursion depth (prevents malicious fonts)
#define MAX_COMPOUND_DEPTH 32
#define MAX_EXPANDED_COMPONENTS 4096
#define MAX_EXPANDED_CONTOURS 65536
#define MAX_EXPANDED_POINTS 1048576

typedef struct GlyfParseBudget {
    int remaining_components;
    int remaining_contours;
    int remaining_points;
} GlyfParseBudget;

static int glyf_get_outline_internal(FontTables* tables, uint16_t glyph_id,
                                     GlyphOutline* out, Arena* arena,
                                     uint16_t* glyph_stack, int depth,
                                     GlyfParseBudget* budget);

// ============================================================================
// Internal: locate glyph data in glyf table via loca
// ============================================================================

// returns pointer to glyph header in glyf table, or NULL for empty/missing glyphs.
// *out_len is set to the glyph data length.
static const uint8_t* glyf_locate(FontTables* tables, uint16_t glyph_id, uint32_t* out_len) {
    HeadTable* head = font_tables_get_head(tables);
    MaxpTable* maxp = font_tables_get_maxp(tables);
    if (!head || !maxp) return NULL;
    if (glyph_id >= maxp->num_glyphs) return NULL;

    uint32_t loca_len = 0, glyf_len = 0;
    const uint8_t* loca = font_tables_find(tables, FONT_TAG('l','o','c','a'), &loca_len);
    const uint8_t* glyf = font_tables_find(tables, FONT_TAG('g','l','y','f'), &glyf_len);
    if (!loca || !glyf) return NULL;

    uint32_t offset, next_offset;
    if (head->index_to_loc_format == 0) {
        // short format: uint16 × 2
        uint32_t idx = (uint32_t)glyph_id * 2;
        if (!font_data_range_valid(loca_len, idx, 4)) return NULL;
        offset      = (uint32_t)rd16(loca + idx) * 2;
        next_offset = (uint32_t)rd16(loca + idx + 2) * 2;
    } else {
        // long format: uint32
        uint32_t idx = (uint32_t)glyph_id * 4;
        if (!font_data_range_valid(loca_len, idx, 8)) return NULL;
        offset      = rd32(loca + idx);
        next_offset = rd32(loca + idx + 4);
    }

    // empty glyph (e.g. space)
    if (offset >= next_offset || next_offset > glyf_len ||
        !font_data_range_valid(glyf_len, offset, 10)) return NULL;

    if (out_len) *out_len = next_offset - offset;
    return glyf + offset;
}

// ============================================================================
// Internal: parse a simple glyph
// ============================================================================

static int parse_simple_glyph(const uint8_t* data, uint32_t data_len,
                               int num_contours, GlyphOutline* out, Arena* arena,
                               GlyfParseBudget* budget) {
    if (num_contours <= 0) return -1;
    if (!budget || num_contours > budget->remaining_contours) return -1;
    budget->remaining_contours -= num_contours;

    // read contour end points
    size_t pos = 10; // skip glyph header (numberOfContours + bbox = 10 bytes)
    size_t endpoint_bytes = (size_t)num_contours * 2;
    if (!font_data_range_valid(data_len, pos, endpoint_bytes)) return -1;

    uint16_t* end_pts = (uint16_t*)arena_alloc(arena, (size_t)num_contours * sizeof(uint16_t));
    if (!end_pts) return -1;

    for (int i = 0; i < num_contours; i++) {
        end_pts[i] = rd16(data + pos);
        if (i > 0 && end_pts[i] <= end_pts[i - 1]) return -1;
        pos += 2;
    }

    int total_points = (int)end_pts[num_contours - 1] + 1;
    if (total_points <= 0 || total_points > 65535) return -1;
    if (total_points > budget->remaining_points) return -1;
    budget->remaining_points -= total_points;

    // skip instructions
    if (!font_data_range_valid(data_len, pos, 2)) return -1;
    uint16_t inst_len = rd16(data + pos);
    pos += 2;
    if (!font_data_range_valid(data_len, pos, inst_len)) return -1;
    pos += inst_len;

    // read flags (with repeat expansion)
    uint8_t* flags = (uint8_t*)arena_alloc(arena, (size_t)total_points);
    if (!flags) return -1;

    int fi = 0;
    while (fi < total_points && pos < data_len) {
        uint8_t flag = data[pos++];
        flags[fi++] = flag;
        if (flag & GLYF_FLAG_REPEAT) {
            if (pos >= data_len) return -1;
            uint8_t repeat_count = data[pos++];
            if ((int)repeat_count > total_points - fi) return -1;
            for (int r = 0; r < repeat_count; r++) {
                flags[fi++] = flag;
            }
        }
    }
    if (fi != total_points) return -1;

    // read x coordinates (delta-encoded)
    int16_t* x_coords = (int16_t*)arena_alloc(arena, (size_t)total_points * sizeof(int16_t));
    if (!x_coords) return -1;

    int16_t x = 0;
    for (int i = 0; i < total_points; i++) {
        if (flags[i] & GLYF_FLAG_X_SHORT) {
            if (pos >= data_len) return -1;
            int16_t dx = (int16_t)data[pos++];
            x += (flags[i] & GLYF_FLAG_X_SAME) ? dx : -dx;
        } else {
            if (flags[i] & GLYF_FLAG_X_SAME) {
                // x is the same as previous (delta = 0)
            } else {
                if (!font_data_range_valid(data_len, pos, 2)) return -1;
                x += rd16s(data + pos);
                pos += 2;
            }
        }
        x_coords[i] = x;
    }

    // read y coordinates (delta-encoded)
    int16_t* y_coords = (int16_t*)arena_alloc(arena, (size_t)total_points * sizeof(int16_t));
    if (!y_coords) return -1;

    int16_t y = 0;
    for (int i = 0; i < total_points; i++) {
        if (flags[i] & GLYF_FLAG_Y_SHORT) {
            if (pos >= data_len) return -1;
            int16_t dy = (int16_t)data[pos++];
            y += (flags[i] & GLYF_FLAG_Y_SAME) ? dy : -dy;
        } else {
            if (flags[i] & GLYF_FLAG_Y_SAME) {
                // y is the same as previous (delta = 0)
            } else {
                if (!font_data_range_valid(data_len, pos, 2)) return -1;
                y += rd16s(data + pos);
                pos += 2;
            }
        }
        y_coords[i] = y;
    }

    // build contours with implicit on-curve midpoint insertion
    out->contours = (GlyfContour*)arena_alloc(arena, (size_t)num_contours * sizeof(GlyfContour));
    if (!out->contours) return -1;
    out->num_contours = num_contours;

    int start = 0;
    for (int c = 0; c < num_contours; c++) {
        int end = (int)end_pts[c];
        int raw_count = end - start + 1;
        if (raw_count <= 0) {
            out->contours[c].points = NULL;
            out->contours[c].num_points = 0;
            start = end + 1;
            continue;
        }

        // worst case: each consecutive off-curve pair inserts one midpoint
        // max expansion is roughly 2× the raw count
        int max_points = raw_count * 2;
        GlyfPoint* pts = (GlyfPoint*)arena_alloc(arena, (size_t)max_points * sizeof(GlyfPoint));
        if (!pts) return -1;

        int np = 0;

        for (int i = start; i <= end; i++) {
            bool cur_on = (flags[i] & GLYF_FLAG_ON_CURVE) != 0;
            float cx = (float)x_coords[i];
            float cy = (float)y_coords[i];

            if (np > 0 && !cur_on) {
                // check if previous point was also off-curve
                if (!pts[np - 1].on_curve) {
                    // insert implicit on-curve midpoint
                    pts[np].x = (pts[np - 1].x + cx) * 0.5f;
                    pts[np].y = (pts[np - 1].y + cy) * 0.5f;
                    pts[np].on_curve = true;
                    np++;
                }
            }

            pts[np].x = cx;
            pts[np].y = cy;
            pts[np].on_curve = cur_on;
            np++;
        }

        // handle wrap-around: if first and last are both off-curve,
        // insert midpoint at the boundary
        if (np >= 2 && !pts[0].on_curve && !pts[np - 1].on_curve) {
            pts[np].x = (pts[np - 1].x + pts[0].x) * 0.5f;
            pts[np].y = (pts[np - 1].y + pts[0].y) * 0.5f;
            pts[np].on_curve = true;
            np++;
        }

        out->contours[c].points = pts;
        out->contours[c].num_points = np;

        start = end + 1;
    }

    return 0;
}

// ============================================================================
// Internal: parse compound (composite) glyph
// ============================================================================

static int parse_compound_glyph(FontTables* tables, const uint8_t* data, uint32_t data_len,
                                 GlyphOutline* out, Arena* arena,
                                 uint16_t* glyph_stack, int depth,
                                 GlyfParseBudget* budget) {
    if (depth >= MAX_COMPOUND_DEPTH) {
        log_debug("font_glyf: compound glyph recursion depth exceeded");
        return -1;
    }

    // collect all component outlines, then merge
    // use a temporary array on arena
    int max_components = 64;
    GlyphOutline* components = (GlyphOutline*)arena_alloc(arena, (size_t)max_components * sizeof(GlyphOutline));
    if (!components) return -1;

    int num_components = 0;
    int parsed_components = 0;
    int total_contours = 0;
    size_t pos = 10; // skip glyph header

    uint16_t flags;
    do {
        if (!font_data_range_valid(data_len, pos, 4)) return -1;
        flags = rd16(data + pos);
        uint16_t glyph_id = rd16(data + pos + 2);
        pos += 4;

        // read translation
        float tx = 0, ty = 0;
        if (flags & COMP_FLAG_ARG_1_AND_2_ARE_WORDS) {
            if (!font_data_range_valid(data_len, pos, 4)) return -1;
            if (flags & COMP_FLAG_ARGS_ARE_XY_VALUES) {
                tx = (float)rd16s(data + pos);
                ty = (float)rd16s(data + pos + 2);
            }
            pos += 4;
        } else {
            if (!font_data_range_valid(data_len, pos, 2)) return -1;
            if (flags & COMP_FLAG_ARGS_ARE_XY_VALUES) {
                tx = (float)(int8_t)data[pos];
                ty = (float)(int8_t)data[pos + 1];
            }
            pos += 2;
        }

        // read scale/transform
        float a = 1.0f, b = 0.0f, c_val = 0.0f, d = 1.0f;
        if (flags & COMP_FLAG_WE_HAVE_A_SCALE) {
            if (!font_data_range_valid(data_len, pos, 2)) return -1;
            a = d = (float)rd16s(data + pos) / 16384.0f;
            pos += 2;
        } else if (flags & COMP_FLAG_WE_HAVE_AN_X_AND_Y_SCALE) {
            if (!font_data_range_valid(data_len, pos, 4)) return -1;
            a = (float)rd16s(data + pos) / 16384.0f;
            d = (float)rd16s(data + pos + 2) / 16384.0f;
            pos += 4;
        } else if (flags & COMP_FLAG_WE_HAVE_A_TWO_BY_TWO) {
            if (!font_data_range_valid(data_len, pos, 8)) return -1;
            a     = (float)rd16s(data + pos) / 16384.0f;
            b     = (float)rd16s(data + pos + 2) / 16384.0f;
            c_val = (float)rd16s(data + pos + 4) / 16384.0f;
            d     = (float)rd16s(data + pos + 6) / 16384.0f;
            pos += 8;
        }

        // Bound every component record, including empty glyphs, so malformed
        // chains cannot bypass the work limit by referencing empty outlines.
        if (parsed_components >= max_components) {
            log_debug("font_glyf: too many compound components");
            return -1;
        }
        if (!budget || budget->remaining_components <= 0) {
            log_debug("font_glyf: expanded component budget exceeded");
            return -1;
        }
        budget->remaining_components--;
        parsed_components++;

        GlyphOutline comp = {0};
        int component_result = glyf_get_outline_internal(
            tables, glyph_id, &comp, arena, glyph_stack, depth + 1, budget);
        if (component_result != 0) return -1;
        if (comp.num_contours > 0) {
            // apply affine transform to all points
            for (int ci = 0; ci < comp.num_contours; ci++) {
                GlyfContour* contour = &comp.contours[ci];
                for (int pi = 0; pi < contour->num_points; pi++) {
                    float ox = contour->points[pi].x;
                    float oy = contour->points[pi].y;
                    contour->points[pi].x = a * ox + c_val * oy + tx;
                    contour->points[pi].y = b * ox + d * oy + ty;
                }
            }
            components[num_components] = comp;
            total_contours += comp.num_contours;
            num_components++;
        }
    } while (flags & COMP_FLAG_MORE_COMPONENTS);

    if (total_contours == 0) {
        out->num_contours = 0;
        out->contours = NULL;
        return 0;
    }

    // merge all component contours into a single outline
    out->contours = (GlyfContour*)arena_alloc(arena, (size_t)total_contours * sizeof(GlyfContour));
    if (!out->contours) return -1;
    out->num_contours = total_contours;

    int dst = 0;
    for (int i = 0; i < num_components; i++) {
        for (int ci = 0; ci < components[i].num_contours; ci++) {
            out->contours[dst++] = components[i].contours[ci];
        }
    }

    return 0;
}

// ============================================================================
// Public API
// ============================================================================

static int glyf_get_outline_internal(FontTables* tables, uint16_t glyph_id,
                                     GlyphOutline* out, Arena* arena,
                                     uint16_t* glyph_stack, int depth,
                                     GlyfParseBudget* budget) {
    if (!tables || !out || !arena) return -1;
    memset(out, 0, sizeof(GlyphOutline));

    if (depth >= MAX_COMPOUND_DEPTH) {
        log_debug("font_glyf: compound glyph recursion depth exceeded");
        return -1;
    }
    for (int i = 0; i < depth; i++) {
        if (glyph_stack[i] == glyph_id) {
            log_debug("font_glyf: compound glyph cycle rejected");
            return -1;
        }
    }
    glyph_stack[depth] = glyph_id;

    uint32_t glyph_len = 0;
    const uint8_t* data = glyf_locate(tables, glyph_id, &glyph_len);
    if (!data) {
        // empty glyph (space, .notdef, etc.)
        return 0;
    }

    // read glyph header
    int16_t num_contours = rd16s(data + 0);
    out->x_min = rd16s(data + 2);
    out->y_min = rd16s(data + 4);
    out->x_max = rd16s(data + 6);
    out->y_max = rd16s(data + 8);

    if (num_contours >= 0) {
        // simple glyph
        return parse_simple_glyph(data, glyph_len, num_contours, out, arena, budget);
    } else if (num_contours == -1) {
        // compound glyph
        int result = parse_compound_glyph(tables, data, glyph_len, out, arena,
                                          glyph_stack, depth, budget);
        if (result == 0 && out->num_contours > 0) {
            // recompute bounding box from actual points
            float xmin = 1e30f, ymin = 1e30f, xmax = -1e30f, ymax = -1e30f;
            for (int c = 0; c < out->num_contours; c++) {
                for (int p = 0; p < out->contours[c].num_points; p++) {
                    float px = out->contours[c].points[p].x;
                    float py = out->contours[c].points[p].y;
                    if (px < xmin) xmin = px;
                    if (py < ymin) ymin = py;
                    if (px > xmax) xmax = px;
                    if (py > ymax) ymax = py;
                }
            }
            out->x_min = (int16_t)floorf(xmin);
            out->y_min = (int16_t)floorf(ymin);
            out->x_max = (int16_t)ceilf(xmax);
            out->y_max = (int16_t)ceilf(ymax);
        }
        return result;
    }
    return -1;
}

int glyf_get_outline(FontTables* tables, uint16_t glyph_id,
                     GlyphOutline* out, Arena* arena) {
    uint16_t glyph_stack[MAX_COMPOUND_DEPTH];
    GlyfParseBudget budget = {
        MAX_EXPANDED_COMPONENTS,
        MAX_EXPANDED_CONTOURS,
        MAX_EXPANDED_POINTS
    };
    return glyf_get_outline_internal(tables, glyph_id, out, arena,
                                     glyph_stack, 0, &budget);
}

// midpoint insertion guarantees every quadratic segment has an on-curve end.
bool glyf_visit_outline(const GlyphOutline* outline, float sx, float sy,
    float tx, float ty, FontPathVisitFn visitor, void* context) {
    if (!outline || !visitor) return false;
    for (int c = 0; c < outline->num_contours; c++) {
        const GlyfContour* contour = &outline->contours[c];
        if (contour->num_points < 2) continue;
        int first = -1;
        for (int i = 0; i < contour->num_points; i++) {
            if (contour->points[i].on_curve) { first = i; break; }
        }
        if (first < 0) return false;
        float current_x = contour->points[first].x * sx + tx;
        float current_y = contour->points[first].y * sy + ty;
        float args[6] = {current_x, current_y};
        if (!visitor(context, FONT_PATH_MOVE, args, 2)) return false;
        for (int step = 1; step < contour->num_points; step++) {
            const GlyfPoint* point = &contour->points[(first + step) % contour->num_points];
            float x = point->x * sx + tx, y = point->y * sy + ty;
            if (point->on_curve) {
                args[0] = x; args[1] = y;
                if (!visitor(context, FONT_PATH_LINE, args, 2)) return false;
            } else {
                const GlyfPoint* end = &contour->points[(first + step + 1) % contour->num_points];
                float ex = end->x * sx + tx, ey = end->y * sy + ty;
                args[0] = current_x + (x - current_x) * (2.0f / 3.0f);
                args[1] = current_y + (y - current_y) * (2.0f / 3.0f);
                args[2] = ex + (x - ex) * (2.0f / 3.0f);
                args[3] = ey + (y - ey) * (2.0f / 3.0f);
                args[4] = ex; args[5] = ey;
                if (!visitor(context, FONT_PATH_CUBIC, args, 6)) return false;
                x = ex; y = ey; step++;
            }
            current_x = x; current_y = y;
        }
        if (!visitor(context, FONT_PATH_CLOSE, NULL, 0)) return false;
    }
    return true;
}
