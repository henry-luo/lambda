// tex_hlist.cpp - Horizontal List Builder Implementation
//
// Converts text to TeX horizontal lists with ligatures and kerning.

#include "tex_hlist.hpp"
#include "lib/log.h"
#include <cstring>

namespace tex {

// ============================================================================
// Standard Ligatures
// ============================================================================

// Standard TeX ligatures (from CMR fonts)
static const LigatureRule STANDARD_LIGATURES[] = {
    {'f', 'f', 0xFB00, "ff"},   // ff ligature
    {'f', 'i', 0xFB01, "fi"},   // fi ligature
    {'f', 'l', 0xFB02, "fl"},   // fl ligature
    // Note: ffi and ffl are handled as ff + i/l
};
static const int STANDARD_LIGATURE_COUNT = 3;

const LigatureRule* get_standard_ligatures(int* count) {
    *count = STANDARD_LIGATURE_COUNT;
    return STANDARD_LIGATURES;
}

// ============================================================================
// Font Selection
// ============================================================================

bool set_font(HListContext& ctx, const char* font_name, float size_pt) {
    TFMFont* tfm = ctx.fonts->get_font(font_name);
    if (!tfm) {
        log_error("tex_hlist: cannot load font %s", font_name);
        return false;
    }

    ctx.current_tfm = tfm;
    ctx.current_font.name = font_name;
    ctx.current_font.size_pt = size_pt;
    ctx.current_font.tfm_index = 0;  // TODO: proper index management

    log_debug("tex_hlist: set font %s at %.1fpt", font_name, size_pt);
    return true;
}

void get_char_metrics(
    HListContext& ctx,
    int32_t codepoint,
    float* width,
    float* height,
    float* depth,
    float* italic
) {
    if (!ctx.current_tfm || codepoint < 0 || codepoint > 127) {
        // Fallback for unknown characters
        float size = ctx.current_font.size_pt;
        *width = size * 0.5f;
        *height = size * 0.7f;
        *depth = 0;
        *italic = 0;
        return;
    }

    float scale = ctx.current_font.size_pt / ctx.current_tfm->design_size;
    *width = ctx.current_tfm->char_width(codepoint) * scale;
    *height = ctx.current_tfm->char_height(codepoint) * scale;
    *depth = ctx.current_tfm->char_depth(codepoint) * scale;
    *italic = ctx.current_tfm->char_italic(codepoint) * scale;
}

// ============================================================================
// Character Node Creation
// ============================================================================

static TexNode* make_char_node(HListContext& ctx, int32_t codepoint) {
    float w, h, d, i;
    get_char_metrics(ctx, codepoint, &w, &h, &d, &i);

    TexNode* node = make_char(ctx.arena, codepoint, ctx.current_font);
    // Keep dimensions in points for TeX internal units
    node->width = w;
    node->height = h;
    node->depth = d;
    node->italic = i;

    return node;
}

// ============================================================================
// Inter-word Glue
// ============================================================================

TexNode* make_interword_glue(HListContext& ctx) {
    if (!ctx.current_tfm) {
        // Fallback: 1/3 em space (in points for TeX internal units)
        float em = ctx.current_font.size_pt;
        Glue g = Glue::flexible(
            em / 3.0f,
            em / 6.0f,
            em / 9.0f
        );
        return make_glue(ctx.arena, g, "interword");
    }

    // TFM space values are already in points (scaled by design size)
    // Keep them in points for TeX line breaking
    float scale = ctx.current_font.size_pt / ctx.current_tfm->design_size;
    Glue g = Glue::flexible(
        ctx.current_tfm->space * scale,
        ctx.current_tfm->space_stretch * scale,
        ctx.current_tfm->space_shrink * scale
    );

    return make_glue(ctx.arena, g, "interword");
}

// ============================================================================
// Word to Nodes
// ============================================================================

TexNode* word_to_nodes(const char* word, size_t len, HListContext& ctx) {
    if (len == 0) return nullptr;

    TexNode* first = nullptr;
    TexNode* last = nullptr;

    // Create character nodes
    const char* p = word;
    const char* end = word + len;

    while (p < end) {
        // Decode UTF-8 (simplified - assumes ASCII for now)
        int32_t cp = (unsigned char)*p++;

        // Handle UTF-8 multi-byte sequences
        if (cp >= 0xC0 && p < end) {
            if (cp < 0xE0) {
                cp = ((cp & 0x1F) << 6) | (*p++ & 0x3F);
            } else if (cp < 0xF0 && p + 1 < end) {
                cp = ((cp & 0x0F) << 12) | ((*p & 0x3F) << 6) | (*(p+1) & 0x3F);
                p += 2;
            } else if (p + 2 < end) {
                cp = ((cp & 0x07) << 18) | ((*p & 0x3F) << 12) |
                     ((*(p+1) & 0x3F) << 6) | (*(p+2) & 0x3F);
                p += 3;
            }
        }

        TexNode* node = make_char_node(ctx, cp);

        if (!first) {
            first = node;
            last = node;
        } else {
            last->next_sibling = node;
            node->prev_sibling = last;
            last = node;
        }
    }

    return first;
}

// ============================================================================
// Ligature Processing
// ============================================================================

void apply_ligatures(TexNode* first, HListContext& ctx) {
    if (!ctx.apply_ligatures) return;

    TexNode* node = first;
    while (node && node->next_sibling) {
        if (node->node_class != NodeClass::Char) {
            node = node->next_sibling;
            continue;
        }

        TexNode* next = node->next_sibling;
        if (next->node_class != NodeClass::Char) {
            node = next;
            continue;
        }

        int32_t c1 = node->content.ch.codepoint;
        int32_t c2 = next->content.ch.codepoint;

        // Check TFM ligature table first
        int lig_char = 0;
        if (ctx.current_tfm) {
            lig_char = ctx.current_tfm->get_ligature(c1, c2);
        }

        // Fall back to standard ligatures
        if (lig_char == 0) {
            for (int i = 0; i < STANDARD_LIGATURE_COUNT; i++) {
                if (STANDARD_LIGATURES[i].first == c1 &&
                    STANDARD_LIGATURES[i].second == c2) {
                    lig_char = STANDARD_LIGATURES[i].result;
                    break;
                }
            }
        }

        if (lig_char != 0) {
            // Create ligature node
            char orig[3] = {(char)c1, (char)c2, 0};
            TexNode* lig = make_ligature(ctx.arena, lig_char, orig, 2, ctx.current_font);

            // Get metrics for ligature (in points)
            float w, h, d, it;
            get_char_metrics(ctx, lig_char, &w, &h, &d, &it);
            lig->width = w;
            lig->height = h;
            lig->depth = d;
            lig->italic = it;

            // Replace the two nodes with the ligature
            lig->prev_sibling = node->prev_sibling;
            lig->next_sibling = next->next_sibling;

            if (node->prev_sibling) {
                node->prev_sibling->next_sibling = lig;
            }
            if (next->next_sibling) {
                next->next_sibling->prev_sibling = lig;
            }

            // Continue from ligature (may form more ligatures like ffi)
            node = lig;
        } else {
            node = node->next_sibling;
        }
    }
}

// ============================================================================
// Kerning
// ============================================================================

void apply_kerning(TexNode* first, HListContext& ctx) {
    if (!ctx.apply_kerning || !ctx.current_tfm) return;

    float scale = ctx.current_font.size_pt / ctx.current_tfm->design_size;

    TexNode* node = first;
    while (node && node->next_sibling) {
        TexNode* next = node->next_sibling;

        // Get codepoints (handle both Char and Ligature nodes)
        int32_t c1 = 0, c2 = 0;

        if (node->node_class == NodeClass::Char) {
            c1 = node->content.ch.codepoint;
        } else if (node->node_class == NodeClass::Ligature) {
            c1 = node->content.lig.codepoint;
        }

        if (next->node_class == NodeClass::Char) {
            c2 = next->content.ch.codepoint;
        } else if (next->node_class == NodeClass::Ligature) {
            c2 = next->content.lig.codepoint;
        }

        if (c1 && c2) {
            float kern_raw = ctx.current_tfm->get_kern(c1, c2);
            float kern = kern_raw * scale;
            if (kern != 0) {
                // Insert kern node (keep in points)
                TexNode* kern_node = make_kern(ctx.arena, kern);

                kern_node->prev_sibling = node;
                kern_node->next_sibling = next;
                node->next_sibling = kern_node;
                next->prev_sibling = kern_node;
            }
        }

        node = node->next_sibling;
        // Skip the kern node we may have just inserted
        if (node && node->node_class == NodeClass::Kern) {
            node = node->next_sibling;
        }
    }
}

// ============================================================================
// Text to HList
// ============================================================================

TexNode* text_to_hlist(const char* text, size_t len, HListContext& ctx) {
    if (!text || len == 0) return nullptr;

    // Create HList container
    TexNode* hlist = make_hlist(ctx.arena);

    const char* p = text;
    const char* end = text + len;
    const char* word_start = nullptr;

    while (p <= end) {
        bool at_end = (p == end);
        bool is_sp = !at_end && is_space((unsigned char)*p);

        if (is_sp || at_end) {
            // End of word
            if (word_start) {
                size_t word_len = p - word_start;
                TexNode* nodes = word_to_nodes(word_start, word_len, ctx);

                if (nodes) {
                    // Apply ligatures within the word
                    apply_ligatures(nodes, ctx);

                    // Apply kerning within the word
                    apply_kerning(nodes, ctx);

                    // Add all nodes to hlist
                    for (TexNode* n = nodes; n; ) {
                        TexNode* next = n->next_sibling;
                        n->prev_sibling = nullptr;
                        n->next_sibling = nullptr;
                        hlist->append_child(n);
                        n = next;
                    }
                }

                word_start = nullptr;
            }

            // Add inter-word glue for space
            if (is_sp && p + 1 < end) {
                hlist->append_child(make_interword_glue(ctx));
            }

            if (at_end) break;
            p++;
        } else {
            // Start or continue word
            if (!word_start) {
                word_start = p;
            }
            p++;
        }
    }

    // Measure the hlist
    HListDimensions dim = measure_hlist(hlist);
    hlist->width = dim.width;
    hlist->height = dim.height;
    hlist->depth = dim.depth;

    return hlist;
}

// ============================================================================
// HList Measurement
// ============================================================================

HListDimensions measure_hlist(TexNode* hlist) {
    HListDimensions dim = {};
    memset(dim.total_stretch, 0, sizeof(dim.total_stretch));
    memset(dim.total_shrink, 0, sizeof(dim.total_shrink));

    for (TexNode* n = hlist->first_child; n; n = n->next_sibling) {
        dim.width += n->width;

        // Track max height and depth
        float h = n->height - n->shift;
        float d = n->depth + n->shift;
        if (h > dim.height) dim.height = h;
        if (d > dim.depth) dim.depth = d;

        // Accumulate glue stretch/shrink
        if (n->node_class == NodeClass::Glue) {
            const Glue& g = n->content.glue.spec;
            dim.total_stretch[(int)g.stretch_order] += g.stretch;
            dim.total_shrink[(int)g.shrink_order] += g.shrink;
        }
    }

    return dim;
}

// ============================================================================
// Glue Setting
// ============================================================================

float set_hlist_glue(TexNode* hlist, float target_width) {
    HListDimensions dim = measure_hlist(hlist);

    float excess = target_width - dim.width;
    if (excess == 0) return 0;

    float ratio = 0;
    GlueOrder order = GlueOrder::Normal;

    if (excess > 0) {
        // Need to stretch
        // Find highest order with non-zero stretch
        for (int o = 3; o >= 0; o--) {
            if (dim.total_stretch[o] > 0) {
                order = (GlueOrder)o;
                ratio = excess / dim.total_stretch[o];
                break;
            }
        }
    } else {
        // Need to shrink
        excess = -excess;
        for (int o = 3; o >= 0; o--) {
            if (dim.total_shrink[o] > 0) {
                order = (GlueOrder)o;
                ratio = -excess / dim.total_shrink[o];
                break;
            }
        }
    }

    // Apply glue setting to all glue nodes
    for (TexNode* n = hlist->first_child; n; n = n->next_sibling) {
        if (n->node_class == NodeClass::Glue) {
            const Glue& g = n->content.glue.spec;

            if (ratio > 0 && g.stretch_order == order) {
                n->width = g.space + ratio * g.stretch;
            } else if (ratio < 0 && g.shrink_order == order) {
                n->width = g.space + ratio * g.shrink;
            }
        }
    }

    // Update hlist dimensions
    hlist->width = target_width;

    // Store glue set info
    if (hlist->node_class == NodeClass::HList) {
        hlist->content.list.glue_set.ratio = ratio;
        hlist->content.list.glue_set.order = order;
        hlist->content.list.glue_set.is_stretch = (ratio > 0);
    }

    return ratio;
}

TexNode* hlist_to_hbox(TexNode* hlist, float width, Arena* arena) {
    TexNode* hbox = make_hbox(arena, width);

    // Move children from hlist to hbox
    hbox->first_child = hlist->first_child;
    hbox->last_child = hlist->last_child;

    for (TexNode* n = hbox->first_child; n; n = n->next_sibling) {
        n->parent = hbox;
    }

    hlist->first_child = nullptr;
    hlist->last_child = nullptr;

    // Set glue
    set_hlist_glue(hbox, width);

    hbox->height = hlist->height;
    hbox->depth = hlist->depth;
    hbox->content.box.set_width = width;

    return hbox;
}

} // namespace tex
