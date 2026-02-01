// tex_node.cpp - Implementation of unified TeX node system
//
// Provides node manipulation and common glue definitions.

#include "tex_node.hpp"
#include "lib/log.h"

namespace tex {

// ============================================================================
// Node Class Names (for debugging)
// ============================================================================

const char* node_class_name(NodeClass nc) {
    switch (nc) {
        case NodeClass::Char:       return "Char";
        case NodeClass::Ligature:   return "Ligature";
        case NodeClass::HList:      return "HList";
        case NodeClass::VList:      return "VList";
        case NodeClass::HBox:       return "HBox";
        case NodeClass::VBox:       return "VBox";
        case NodeClass::VTop:       return "VTop";
        case NodeClass::Glue:       return "Glue";
        case NodeClass::Kern:       return "Kern";
        case NodeClass::Penalty:    return "Penalty";
        case NodeClass::Rule:       return "Rule";
        case NodeClass::MathList:   return "MathList";
        case NodeClass::MathChar:   return "MathChar";
        case NodeClass::MathOp:     return "MathOp";
        case NodeClass::Fraction:   return "Fraction";
        case NodeClass::Radical:    return "Radical";
        case NodeClass::Delimiter:  return "Delimiter";
        case NodeClass::Accent:     return "Accent";
        case NodeClass::Scripts:    return "Scripts";
        case NodeClass::MTable:     return "MTable";
        case NodeClass::MTableColumn: return "MTableColumn";
        case NodeClass::Paragraph:  return "Paragraph";
        case NodeClass::Page:       return "Page";
        case NodeClass::Mark:       return "Mark";
        case NodeClass::Insert:     return "Insert";
        case NodeClass::Adjust:     return "Adjust";
        case NodeClass::Whatsit:    return "Whatsit";
        case NodeClass::Disc:       return "Disc";
        case NodeClass::Error:      return "Error";
        default:                    return "Unknown";
    }
}

// ============================================================================
// TexNode Child Management
// ============================================================================

void TexNode::append_child(TexNode* child) {
    if (!child) return;

    child->parent = this;
    child->next_sibling = nullptr;
    child->prev_sibling = last_child;

    if (last_child) {
        last_child->next_sibling = child;
    } else {
        first_child = child;
    }
    last_child = child;

    // Update list count if applicable
    if (node_class == NodeClass::HList || node_class == NodeClass::VList) {
        content.list.child_count++;
    }
}

void TexNode::prepend_child(TexNode* child) {
    if (!child) return;

    child->parent = this;
    child->prev_sibling = nullptr;
    child->next_sibling = first_child;

    if (first_child) {
        first_child->prev_sibling = child;
    } else {
        last_child = child;
    }
    first_child = child;

    if (node_class == NodeClass::HList || node_class == NodeClass::VList) {
        content.list.child_count++;
    }
}

void TexNode::insert_after(TexNode* sibling, TexNode* child) {
    if (!child) return;

    if (!sibling) {
        prepend_child(child);
        return;
    }

    child->parent = this;
    child->prev_sibling = sibling;
    child->next_sibling = sibling->next_sibling;

    if (sibling->next_sibling) {
        sibling->next_sibling->prev_sibling = child;
    } else {
        last_child = child;
    }
    sibling->next_sibling = child;

    if (node_class == NodeClass::HList || node_class == NodeClass::VList) {
        content.list.child_count++;
    }
}

void TexNode::remove_child(TexNode* child) {
    if (!child || child->parent != this) return;

    if (child->prev_sibling) {
        child->prev_sibling->next_sibling = child->next_sibling;
    } else {
        first_child = child->next_sibling;
    }

    if (child->next_sibling) {
        child->next_sibling->prev_sibling = child->prev_sibling;
    } else {
        last_child = child->prev_sibling;
    }

    child->parent = nullptr;
    child->prev_sibling = nullptr;
    child->next_sibling = nullptr;

    if (node_class == NodeClass::HList || node_class == NodeClass::VList) {
        content.list.child_count--;
    }
}

int TexNode::child_count() const {
    if (node_class == NodeClass::HList || node_class == NodeClass::VList) {
        return content.list.child_count;
    }
    int count = 0;
    for (TexNode* c = first_child; c; c = c->next_sibling) {
        count++;
    }
    return count;
}

// ============================================================================
// Common Named Glue Definitions
// ============================================================================

Glue interword_glue(const FontSpec& font) {
    // Standard interword space: typically font_size/3 with stretch/shrink
    // Keep in points for TeX internal units
    float space = font.size_pt * 0.333f;
    float stretch = font.size_pt * 0.166f;
    float shrink = font.size_pt * 0.111f;
    return Glue::flexible(space, stretch, shrink);
}

Glue hfil_glue() {
    return Glue::fil(0, 1.0f);
}

Glue hfill_glue() {
    return Glue::fill(0, 1.0f);
}

Glue hss_glue() {
    // Both fil stretch and fil shrink
    Glue g = Glue::fil(0, 1.0f);
    g.shrink = 1.0f;
    g.shrink_order = GlueOrder::Fil;
    return g;
}

Glue vfil_glue() {
    return Glue::fil(0, 1.0f);
}

Glue vfill_glue() {
    return Glue::fill(0, 1.0f);
}

Glue vss_glue() {
    Glue g = Glue::fil(0, 1.0f);
    g.shrink = 1.0f;
    g.shrink_order = GlueOrder::Fil;
    return g;
}

Glue parskip_glue(float base) {
    // Parskip: base + 1pt stretch (keep in points)
    return Glue::flexible(base, 1.0f, 0);
}

Glue baselineskip_glue(float skip) {
    // Baselineskip is usually fixed (keep in points)
    return Glue::fixed(skip);
}

} // namespace tex
