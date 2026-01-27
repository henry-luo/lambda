#include "html5_parser.h"
#include "../../../lib/log.h"
#include "../../mark_builder.hpp"
#include "../../mark_reader.hpp"
#include "../../mark_editor.hpp"
#include <string.h>
#include <assert.h>

// ============================================================================
// SVG/MathML NAMESPACE HANDLING
// Per WHATWG HTML5 spec: https://html.spec.whatwg.org/multipage/parsing.html#creating-and-inserting-nodes
// ============================================================================

// SVG attribute name replacements (lowercase -> correct case)
// HTML5 tokenizer lowercases all attribute names, but SVG requires specific casing
static const char* svg_attribute_replacements[][2] = {
    {"attributename", "attributeName"},
    {"attributetype", "attributeType"},
    {"basefrequency", "baseFrequency"},
    {"baseprofile", "baseProfile"},
    {"calcmode", "calcMode"},
    {"clippathunits", "clipPathUnits"},
    {"diffuseconstant", "diffuseConstant"},
    {"edgemode", "edgeMode"},
    {"filterunits", "filterUnits"},
    {"glyphref", "glyphRef"},
    {"gradienttransform", "gradientTransform"},
    {"gradientunits", "gradientUnits"},
    {"kernelmatrix", "kernelMatrix"},
    {"kernelunitlength", "kernelUnitLength"},
    {"keypoints", "keyPoints"},
    {"keysplines", "keySplines"},
    {"keytimes", "keyTimes"},
    {"lengthadjust", "lengthAdjust"},
    {"limitingconeangle", "limitingConeAngle"},
    {"markerheight", "markerHeight"},
    {"markerunits", "markerUnits"},
    {"markerwidth", "markerWidth"},
    {"maskcontentunits", "maskContentUnits"},
    {"maskunits", "maskUnits"},
    {"numoctaves", "numOctaves"},
    {"pathlength", "pathLength"},
    {"patterncontentunits", "patternContentUnits"},
    {"patterntransform", "patternTransform"},
    {"patternunits", "patternUnits"},
    {"pointsatx", "pointsAtX"},
    {"pointsaty", "pointsAtY"},
    {"pointsatz", "pointsAtZ"},
    {"preservealpha", "preserveAlpha"},
    {"preserveaspectratio", "preserveAspectRatio"},
    {"primitiveunits", "primitiveUnits"},
    {"refx", "refX"},
    {"refy", "refY"},
    {"repeatcount", "repeatCount"},
    {"repeatdur", "repeatDur"},
    {"requiredextensions", "requiredExtensions"},
    {"requiredfeatures", "requiredFeatures"},
    {"specularconstant", "specularConstant"},
    {"specularexponent", "specularExponent"},
    {"spreadmethod", "spreadMethod"},
    {"startoffset", "startOffset"},
    {"stddeviation", "stdDeviation"},
    {"stitchtiles", "stitchTiles"},
    {"surfacescale", "surfaceScale"},
    {"systemlanguage", "systemLanguage"},
    {"tablevalues", "tableValues"},
    {"targetx", "targetX"},
    {"targety", "targetY"},
    {"textlength", "textLength"},
    {"viewbox", "viewBox"},
    {"viewtarget", "viewTarget"},
    {"xchannelselector", "xChannelSelector"},
    {"ychannelselector", "yChannelSelector"},
    {"zoomandpan", "zoomAndPan"},
    {nullptr, nullptr}
};

// SVG tag name replacements (lowercase -> correct case)
static const char* svg_tag_replacements[][2] = {
    {"altglyph", "altGlyph"},
    {"altglyphdef", "altGlyphDef"},
    {"altglyphitem", "altGlyphItem"},
    {"animatecolor", "animateColor"},
    {"animatemotion", "animateMotion"},
    {"animatetransform", "animateTransform"},
    {"clippath", "clipPath"},
    {"feblend", "feBlend"},
    {"fecolormatrix", "feColorMatrix"},
    {"fecomponenttransfer", "feComponentTransfer"},
    {"fecomposite", "feComposite"},
    {"feconvolvematrix", "feConvolveMatrix"},
    {"fediffuselighting", "feDiffuseLighting"},
    {"fedisplacementmap", "feDisplacementMap"},
    {"fedistantlight", "feDistantLight"},
    {"fedropshadow", "feDropShadow"},
    {"feflood", "feFlood"},
    {"fefunca", "feFuncA"},
    {"fefuncb", "feFuncB"},
    {"fefuncg", "feFuncG"},
    {"fefuncr", "feFuncR"},
    {"fegaussianblur", "feGaussianBlur"},
    {"feimage", "feImage"},
    {"femerge", "feMerge"},
    {"femergenode", "feMergeNode"},
    {"femorphology", "feMorphology"},
    {"feoffset", "feOffset"},
    {"fepointlight", "fePointLight"},
    {"fespecularlighting", "feSpecularLighting"},
    {"fespotlight", "feSpotLight"},
    {"fetile", "feTile"},
    {"feturbulence", "feTurbulence"},
    {"foreignobject", "foreignObject"},
    {"glyphref", "glyphRef"},
    {"lineargradient", "linearGradient"},
    {"radialgradient", "radialGradient"},
    {"textpath", "textPath"},
    {nullptr, nullptr}
};

// Foreign namespace attribute handling (xlink:, xml:, xmlns:)
// These attributes need to preserve their namespace prefixes
static const char* foreign_attributes[][2] = {
    {"xlink:actuate", "xlink:actuate"},
    {"xlink:arcrole", "xlink:arcrole"},
    {"xlink:href", "xlink:href"},
    {"xlink:role", "xlink:role"},
    {"xlink:show", "xlink:show"},
    {"xlink:title", "xlink:title"},
    {"xlink:type", "xlink:type"},
    {"xml:base", "xml:base"},
    {"xml:lang", "xml:lang"},
    {"xml:space", "xml:space"},
    {nullptr, nullptr}
};

// Lookup SVG tag name replacement (returns corrected name or original if no replacement)
static const char* html5_lookup_svg_tag(const char* tag_name) {
    for (int i = 0; svg_tag_replacements[i][0] != nullptr; i++) {
        if (strcmp(tag_name, svg_tag_replacements[i][0]) == 0) {
            return svg_tag_replacements[i][1];
        }
    }
    return tag_name;  // no replacement, return original
}

// Lookup SVG attribute name replacement (returns corrected name or original if no replacement)
static const char* html5_lookup_svg_attr(const char* attr_name) {
    for (int i = 0; svg_attribute_replacements[i][0] != nullptr; i++) {
        if (strcmp(attr_name, svg_attribute_replacements[i][0]) == 0) {
            return svg_attribute_replacements[i][1];
        }
    }
    // Also check foreign attributes
    for (int i = 0; foreign_attributes[i][0] != nullptr; i++) {
        if (strcmp(attr_name, foreign_attributes[i][0]) == 0) {
            return foreign_attributes[i][1];
        }
    }
    return attr_name;  // no replacement, return original
}

// Check if element is in SVG namespace (based on parent chain)
static bool html5_is_in_svg_namespace(Html5Parser* parser) {
    // Walk up the stack looking for an SVG element
    for (int i = (int)parser->open_elements->length - 1; i >= 0; i--) {
        Element* elem = (Element*)parser->open_elements->items[i].element;
        const char* tag_name = ((TypeElmt*)elem->type)->name.str;

        // Found svg element - we're in SVG namespace
        if (strcmp(tag_name, "svg") == 0) {
            return true;
        }
        // Found html element - we've exited SVG namespace
        if (strcmp(tag_name, "html") == 0 || strcmp(tag_name, "body") == 0 ||
            strcmp(tag_name, "head") == 0 || strcmp(tag_name, "foreignObject") == 0) {
            return false;
        }
    }
    return false;
}

// parser lifecycle
Html5Parser* html5_parser_create(Pool* pool, Arena* arena, Input* input) {
    Html5Parser* parser = (Html5Parser*)pool_calloc(pool, sizeof(Html5Parser));
    parser->pool = pool;
    parser->arena = arena;
    parser->input = input;

    // initialize stacks
    parser->open_elements = list_arena(arena);
    parser->active_formatting = list_arena(arena);
    parser->template_modes = list_arena(arena);

    // initial mode
    parser->mode = HTML5_MODE_INITIAL;
    parser->original_insertion_mode = HTML5_MODE_INITIAL;

    // flags
    parser->scripting_enabled = true;
    parser->frameset_ok = true;
    parser->foster_parenting = false;
    parser->ignore_next_lf = false;
    parser->quirks_mode = false;      // default to standards mode
    parser->limited_quirks_mode = false;

    // temporary buffer (4KB initial capacity)
    parser->temp_buffer_capacity = 4096;
    parser->temp_buffer = (char*)arena_alloc(arena, parser->temp_buffer_capacity);
    parser->temp_buffer_len = 0;

    // text content buffering
    parser->text_buffer = stringbuf_new(pool);
    parser->pending_text_parent = nullptr;

    // foster parent text buffering
    parser->foster_text_buffer = stringbuf_new(pool);
    parser->foster_table_element = nullptr;
    parser->foster_parent_element = nullptr;

    // last start tag (for RCDATA/RAWTEXT end tag matching)
    parser->last_start_tag_name = nullptr;
    parser->last_start_tag_name_len = 0;

    // error collection
    html5_error_list_init(&parser->errors, arena);

    return parser;
}

void html5_parser_destroy(Html5Parser* parser) {
    // memory is pool/arena-managed, nothing to free explicitly
    (void)parser;
}

// stack operations - these implement the "stack of open elements" from WHATWG spec
Element* html5_current_node(Html5Parser* parser) {
    if (parser->open_elements->length == 0) {
        return nullptr;
    }
    return (Element*)parser->open_elements->items[parser->open_elements->length - 1].element;
}

void html5_push_element(Html5Parser* parser, Element* elem) {
    Item item;
    item.element = elem;
    array_append(parser->open_elements, item, parser->pool, parser->arena);
    log_debug("html5: pushed element <%s>, stack depth now %zu", ((TypeElmt*)elem->type)->name.str, parser->open_elements->length);
}

Element* html5_pop_element(Html5Parser* parser) {
    if (parser->open_elements->length == 0) {
        log_error("html5: attempted to pop from empty stack");
        return nullptr;
    }

    Element* elem = (Element*)parser->open_elements->items[parser->open_elements->length - 1].element;
    parser->open_elements->length--;
    log_debug("html5: popped element <%s>, stack depth now %zu", ((TypeElmt*)elem->type)->name.str, parser->open_elements->length);
    return elem;
}

// Helper to recursively find parent of an element in the DOM tree
// Returns the parent element and sets child_pos to the position of target in parent's children
static Element* find_parent_of_element(Element* root, Element* target, int* child_pos) {
    if (root == nullptr || target == nullptr) return nullptr;

    for (size_t i = 0; i < root->length; i++) {
        TypeId type = get_type_id(root->items[i]);
        if (type == LMD_TYPE_ELEMENT) {
            Element* child = root->items[i].element;
            if (child == target) {
                *child_pos = (int)i;
                return root;
            }
            // Recursively search in child
            Element* found = find_parent_of_element(child, target, child_pos);
            if (found != nullptr) {
                return found;
            }
        }
    }
    return nullptr;
}

// scope checking - implements "has an element in scope" algorithms from WHATWG spec
static bool is_scope_marker(const char* tag_name, const char** scope_list, size_t scope_len) {
    for (size_t i = 0; i < scope_len; i++) {
        if (strcmp(tag_name, scope_list[i]) == 0) {
            return true;
        }
    }
    return false;
}

static bool has_element_in_scope_generic(Html5Parser* parser, const char* target_tag_name,
                                          const char** scope_list, size_t scope_len) {
    // traverse stack from top to bottom
    for (int i = (int)parser->open_elements->length - 1; i >= 0; i--) {
        Element* elem = (Element*)parser->open_elements->items[i].element;
        const char* tag_name = ((TypeElmt*)elem->type)->name.str;

        if (strcmp(tag_name, target_tag_name) == 0) {
            return true;
        }

        if (is_scope_marker(tag_name, scope_list, scope_len)) {
            return false;
        }
    }
    return false;
}

bool html5_has_element_in_scope(Html5Parser* parser, const char* tag_name) {
    // standard scope markers: applet, caption, html, table, td, th, marquee, object, template,
    // plus MathML mi, mo, mn, ms, mtext, annotation-xml, and SVG foreignObject, desc, title
    static const char* scope_markers[] = {
        "applet", "caption", "html", "table", "td", "th", "marquee", "object", "template"
    };
    return has_element_in_scope_generic(parser, tag_name, scope_markers, 9);
}

bool html5_has_element_in_button_scope(Html5Parser* parser, const char* tag_name) {
    // button scope = standard scope + button
    static const char* scope_markers[] = {
        "applet", "caption", "html", "table", "td", "th", "marquee", "object", "template", "button"
    };
    return has_element_in_scope_generic(parser, tag_name, scope_markers, 10);
}

bool html5_has_element_in_table_scope(Html5Parser* parser, const char* tag_name) {
    // table scope = html, table, template
    static const char* scope_markers[] = {"html", "table", "template"};
    return has_element_in_scope_generic(parser, tag_name, scope_markers, 3);
}

bool html5_has_element_in_list_item_scope(Html5Parser* parser, const char* tag_name) {
    // list item scope = standard scope + ol, ul
    static const char* scope_markers[] = {
        "applet", "caption", "html", "table", "td", "th", "marquee", "object", "template", "ol", "ul"
    };
    return has_element_in_scope_generic(parser, tag_name, scope_markers, 11);
}

bool html5_has_element_in_select_scope(Html5Parser* parser, const char* tag_name) {
    // select scope = all elements EXCEPT optgroup and option
    for (int i = (int)parser->open_elements->length - 1; i >= 0; i--) {
        Element* elem = (Element*)parser->open_elements->items[i].element;
        const char* elem_tag = ((TypeElmt*)elem->type)->name.str;

        if (strcmp(elem_tag, tag_name) == 0) {
            return true;
        }

        if (strcmp(elem_tag, "optgroup") != 0 && strcmp(elem_tag, "option") != 0) {
            return false;
        }
    }
    return false;
}

// implied end tags - implements "generate implied end tags" from WHATWG spec
void html5_generate_implied_end_tags(Html5Parser* parser) {
    static const char* implied_tags[] = {
        "dd", "dt", "li", "optgroup", "option", "p", "rb", "rp", "rt", "rtc"
    };

    while (parser->open_elements->length > 0) {
        Element* current = html5_current_node(parser);
        const char* tag_name = ((TypeElmt*)current->type)->name.str;

        bool is_implied = false;
        for (size_t i = 0; i < 10; i++) {
            if (strcmp(tag_name, implied_tags[i]) == 0) {
                is_implied = true;
                break;
            }
        }

        if (!is_implied) {
            break;
        }

        html5_pop_element(parser);
    }
}

void html5_generate_implied_end_tags_except(Html5Parser* parser, const char* exception_tag) {
    static const char* implied_tags[] = {
        "dd", "dt", "li", "optgroup", "option", "p", "rb", "rp", "rt", "rtc"
    };

    while (parser->open_elements->length > 0) {
        Element* current = html5_current_node(parser);
        const char* tag_name = ((TypeElmt*)current->type)->name.str;

        if (strcmp(tag_name, exception_tag) == 0) {
            break;
        }

        bool is_implied = false;
        for (size_t i = 0; i < 10; i++) {
            if (strcmp(tag_name, implied_tags[i]) == 0) {
                is_implied = true;
                break;
            }
        }

        if (!is_implied) {
            break;
        }

        html5_pop_element(parser);
    }
}

// close a <p> element in button scope - implements "close a p element" from WHATWG spec
void html5_close_p_element(Html5Parser* parser) {
    // generate implied end tags except for p
    html5_generate_implied_end_tags_except(parser, "p");
    // pop elements until we pop a p element
    while (parser->open_elements->length > 0) {
        Element* current = html5_current_node(parser);
        const char* tag_name = ((TypeElmt*)current->type)->name.str;
        html5_pop_element(parser);
        if (strcmp(tag_name, "p") == 0) {
            break;
        }
    }
}

// active formatting elements - implements "reconstruct the active formatting elements" from WHATWG spec
void html5_reconstruct_active_formatting_elements(Html5Parser* parser) {
    // step 1: if there are no entries in the list, stop
    if (parser->active_formatting->length == 0) {
        return;
    }

    // step 2: if the last entry is a marker or in the stack, stop
    int entry_idx = (int)parser->active_formatting->length - 1;
    Item entry = parser->active_formatting->items[entry_idx];

    if (entry.element == nullptr) {  // marker
        return;
    }

    // check if entry is in stack
    bool in_stack = false;
    for (size_t i = 0; i < parser->open_elements->length; i++) {
        if (parser->open_elements->items[i].element == entry.element) {
            in_stack = true;
            break;
        }
    }
    if (in_stack) {
        return;
    }

    // step 3-6: rewind to find first entry not in stack
    while (entry_idx > 0) {
        entry_idx--;
        entry = parser->active_formatting->items[entry_idx];

        if (entry.element == nullptr) {  // marker
            entry_idx++;
            break;
        }

        in_stack = false;
        for (size_t i = 0; i < parser->open_elements->length; i++) {
            if (parser->open_elements->items[i].element == entry.element) {
                in_stack = true;
                break;
            }
        }
        if (in_stack) {
            entry_idx++;
            break;
        }
    }

    // step 7-10: create and insert elements
    while (entry_idx < (int)parser->active_formatting->length) {
        Element* old_elem = (Element*)parser->active_formatting->items[entry_idx].element;

        // create new element with same name and copy attributes
        MarkBuilder builder(parser->input);
        const char* tag_name = ((TypeElmt*)old_elem->type)->name.str;
        ElementBuilder elem_builder = builder.element(tag_name);

        // Copy attributes from old element to new element using shape iteration
        TypeElmt* old_type = (TypeElmt*)old_elem->type;
        if (old_type && old_type->shape) {
            ShapeEntry* shape = old_type->shape;
            while (shape) {
                if (shape->name) {
                    // Get attribute value from old element
                    ConstItem attr_value = old_elem->get_attr(shape->name->str);
                    TypeId val_type = attr_value.type_id();
                    if (val_type != LMD_TYPE_NULL) {
                        // need to cast const away for attr() - the Item will be copied anyway
                        elem_builder.attr(shape->name->str, *(Item*)&attr_value);
                    }
                }
                shape = shape->next;
            }
        }

        Element* new_elem = elem_builder.final().element;

        // Insert into appropriate place - check for foster parenting
        if (parser->foster_parenting) {
            // Find the last table element in the stack
            Element* table_element = nullptr;
            int table_index = -1;

            for (int i = (int)parser->open_elements->length - 1; i >= 0; i--) {
                Element* el = (Element*)parser->open_elements->items[i].element;
                const char* el_tag = ((TypeElmt*)el->type)->name.str;
                if (strcmp(el_tag, "table") == 0) {
                    table_element = el;
                    table_index = i;
                    break;
                }
            }

            if (table_element != nullptr) {
                // Find the table's DOM parent by searching the DOM tree
                // First try searching the open elements stack (fast path)
                Element* foster_parent = nullptr;
                int table_pos = -1;

                for (int i = table_index - 1; i >= 0; i--) {
                    Element* candidate = (Element*)parser->open_elements->items[i].element;
                    for (size_t j = 0; j < candidate->length; j++) {
                        if (candidate->items[j].element == table_element) {
                            foster_parent = candidate;
                            table_pos = (int)j;
                            break;
                        }
                    }
                    if (foster_parent != nullptr) {
                        break;
                    }
                }

                // If not found in open_elements, search the entire DOM tree
                if (foster_parent == nullptr && parser->document != nullptr) {
                    // Search from body element if it exists, otherwise from document
                    Element* search_root = nullptr;
                    for (size_t i = 0; i < parser->document->length; i++) {
                        TypeId type = get_type_id(parser->document->items[i]);
                        if (type == LMD_TYPE_ELEMENT) {
                            Element* child = parser->document->items[i].element;
                            const char* child_tag = ((TypeElmt*)child->type)->name.str;
                            if (strcmp(child_tag, "html") == 0) {
                                search_root = child;
                                break;
                            }
                        }
                    }
                    if (search_root != nullptr) {
                        foster_parent = find_parent_of_element(search_root, table_element, &table_pos);
                    }
                }

                if (foster_parent != nullptr && table_pos >= 0) {
                    // Insert before the table
                    log_debug("html5_reconstruct_foster: inserting <%s> before table at pos %d", tag_name, table_pos);
                    MarkEditor editor(parser->input);
                    editor.array_insert(Item{.element = foster_parent}, table_pos, Item{.element = new_elem});
                    html5_push_element(parser, new_elem);
                    parser->active_formatting->items[entry_idx].element = new_elem;
                    entry_idx++;
                    continue;
                } else if (table_index > 0) {
                    // Fallback: append to element before table in stack
                    foster_parent = (Element*)parser->open_elements->items[table_index - 1].element;
                    array_append(foster_parent, Item{.element = new_elem}, parser->pool, parser->arena);
                    html5_push_element(parser, new_elem);
                    parser->active_formatting->items[entry_idx].element = new_elem;
                    entry_idx++;
                    continue;
                }
            }
        }

        // Normal insertion: append to current node
        Element* parent = html5_current_node(parser);
        array_append(parent, Item{.element = new_elem}, parser->pool, parser->arena);
        html5_push_element(parser, new_elem);

        // replace entry in active formatting list
        parser->active_formatting->items[entry_idx].element = new_elem;

        entry_idx++;
    }
}

void html5_clear_active_formatting_to_marker(Html5Parser* parser) {
    while (parser->active_formatting->length > 0) {
        Item entry = parser->active_formatting->items[parser->active_formatting->length - 1];
        parser->active_formatting->length--;

        if (entry.element == nullptr) {  // marker
            break;
        }
    }
}

// element insertion helpers
Element* html5_insert_html_element(Html5Parser* parser, Html5Token* token) {
    // Flush any pending text before inserting element
    html5_flush_pending_text(parser);

    // Also flush any pending foster text
    html5_flush_foster_text(parser);

    // Create element with attributes from token
    Element* elem = html5_create_element_for_token(parser, token);

    // Check if foster parenting is enabled (inside table but outside cells)
    if (parser->foster_parenting) {
        // Find the last table element in the stack
        Element* table_element = nullptr;
        int table_index = -1;

        for (int i = (int)parser->open_elements->length - 1; i >= 0; i--) {
            Element* el = (Element*)parser->open_elements->items[i].element;
            const char* el_tag = ((TypeElmt*)el->type)->name.str;
            if (strcmp(el_tag, "table") == 0) {
                table_element = el;
                table_index = i;
                break;
            }
        }

        if (table_element != nullptr) {
            // Per WHATWG spec: Find the table's DOM parent
            // First try searching backwards in the stack
            Element* foster_parent = nullptr;
            int table_pos = -1;

            for (int i = table_index - 1; i >= 0; i--) {
                Element* candidate = (Element*)parser->open_elements->items[i].element;
                // Check if table is a direct child of this element
                for (size_t j = 0; j < candidate->length; j++) {
                    if (candidate->items[j].element == table_element) {
                        foster_parent = candidate;
                        table_pos = (int)j;
                        break;
                    }
                }
                if (foster_parent != nullptr) {
                    break;
                }
            }

            // If not found in open_elements, search the entire DOM tree
            if (foster_parent == nullptr && parser->document != nullptr) {
                Element* search_root = nullptr;
                for (size_t i = 0; i < parser->document->length; i++) {
                    TypeId type = get_type_id(parser->document->items[i]);
                    if (type == LMD_TYPE_ELEMENT) {
                        Element* child = parser->document->items[i].element;
                        const char* child_tag = ((TypeElmt*)child->type)->name.str;
                        if (strcmp(child_tag, "html") == 0) {
                            search_root = child;
                            break;
                        }
                    }
                }
                if (search_root != nullptr) {
                    foster_parent = find_parent_of_element(search_root, table_element, &table_pos);
                }
            }

            if (foster_parent != nullptr && table_pos >= 0) {
                const char* foster_tag = ((TypeElmt*)foster_parent->type)->name.str;
                log_debug("html5_foster: element=<%s> table_index=%d foster_parent=<%s> table_pos=%d",
                          token->tag_name->chars, table_index, foster_tag, table_pos);

                // Insert element before the table in the foster parent
                log_debug("html5: foster parenting element <%s> before table at pos %d",
                          token->tag_name->chars, table_pos);
                MarkEditor editor(parser->input);
                editor.array_insert(Item{.element = foster_parent}, table_pos, Item{.element = elem});

                // push onto stack
                html5_push_element(parser, elem);
                log_debug("html5: inserted element <%s> (foster parented)", token->tag_name->chars);
                return elem;
            } else if (table_index > 0) {
                // Fallback: use element before table in stack as foster parent
                foster_parent = (Element*)parser->open_elements->items[table_index - 1].element;
                log_debug("html5_foster: fallback - element=<%s> foster_parent=<%s>",
                          token->tag_name->chars, ((TypeElmt*)foster_parent->type)->name.str);
                array_append(foster_parent, Item{.element = elem}, parser->pool, parser->arena);
                html5_push_element(parser, elem);
                return elem;
            }
        }
    }

    // Normal insertion: insert into tree
    Element* parent = html5_current_node(parser);
    if (parent != nullptr) {
        array_append((Array*)parent, Item{.element = elem}, parser->pool, parser->arena);
    } else {
        // no parent - must be root html element, add to document
        array_append((Array*)parser->document, Item{.element = elem}, parser->pool, parser->arena);
    }

    // push onto stack
    html5_push_element(parser, elem);

    log_debug("html5: inserted element <%s>", token->tag_name->chars);
    
    // For SVG/MathML self-closing elements, pop immediately
    // Per WHATWG spec: In foreign content, self-closing tags should be immediately closed
    if (token->self_closing && html5_is_in_svg_namespace(parser)) {
        log_debug("html5: self-closing SVG element <%s>, popping immediately", token->tag_name->chars);
        html5_pop_element(parser);
    }
    
    return elem;
}

// Flush pending text buffer to parent element as a single text node
void html5_flush_pending_text(Html5Parser* parser) {
    if (parser->text_buffer->length == 0) {
        return;  // nothing to flush
    }

    log_debug("html5_flush_pending_text: flushing %zu chars: '%.*s'",
              parser->text_buffer->length,
              (int)parser->text_buffer->length,
              parser->text_buffer->str->chars);

    Element* parent = parser->pending_text_parent;
    if (parent == nullptr) {
        parent = html5_current_node(parser);
    }
    if (parent == nullptr) {
        log_error("html5: cannot flush text, no parent element");
        stringbuf_reset(parser->text_buffer);
        parser->pending_text_parent = nullptr;
        return;
    }

    // Convert buffer to String and create text node
    String* text_str = stringbuf_to_string(parser->text_buffer);
    log_debug("html5_flush_pending_text: created String with len=%zu", text_str->len);
    Item text_node = {.item = s2it(text_str)};
    array_append((Array*)parent, text_node, parser->pool, parser->arena);

    // Reset buffer for next text run
    stringbuf_reset(parser->text_buffer);
    parser->pending_text_parent = nullptr;
}

void html5_insert_character(Html5Parser* parser, char c) {
    Element* parent = html5_current_node(parser);
    if (parent == nullptr) {
        log_error("html5: cannot insert character, no current node");
        return;
    }

    // If parent changed, flush previous text first
    if (parser->pending_text_parent != nullptr && parser->pending_text_parent != parent) {
        html5_flush_pending_text(parser);
    }

    // Buffer the character
    log_debug("html5_insert_character: appending '%c' (0x%02x), buffer_len before=%zu", c, (unsigned char)c, parser->text_buffer->length);
    stringbuf_append_char(parser->text_buffer, c);
    parser->pending_text_parent = parent;
}

// Foster parent: insert text/element before the table element
// Flush foster parented text buffer - inserts text before the table element
void html5_flush_foster_text(Html5Parser* parser) {
    if (parser->foster_text_buffer->length == 0) {
        return;  // nothing to flush
    }

    Element* table_element = parser->foster_table_element;
    Element* foster_parent = parser->foster_parent_element;

    if (table_element == nullptr || foster_parent == nullptr) {
        log_error("html5_flush_foster_text: no table or foster parent");
        stringbuf_reset(parser->foster_text_buffer);
        return;
    }

    // Find the table's position in foster parent's children
    int table_pos = -1;
    for (size_t i = 0; i < foster_parent->length; i++) {
        if (foster_parent->items[i].element == table_element) {
            table_pos = (int)i;
            break;
        }
    }

    // Check if there's already a text node immediately before the table
    // that we should merge with (per WHATWG spec - foster text merges)
    if (table_pos > 0) {
        Item prev = foster_parent->items[table_pos - 1];
        TypeId prev_type = get_type_id(prev);
        if (prev_type == LMD_TYPE_STRING) {
            // Merge with existing text node
            String* existing = (String*)prev.string_ptr;
            StringBuf* combined = stringbuf_new(parser->pool);
            stringbuf_append_str_n(combined, existing->chars, existing->len);
            stringbuf_append_str_n(combined, parser->foster_text_buffer->str->chars,
                                   parser->foster_text_buffer->length);
            String* new_str = stringbuf_to_string(combined);
            foster_parent->items[table_pos - 1] = {.item = s2it(new_str)};
            log_debug("html5_flush_foster_text: merged foster text with existing text before table");
            stringbuf_reset(parser->foster_text_buffer);
            parser->foster_table_element = nullptr;
            parser->foster_parent_element = nullptr;
            return;
        }
    }

    // Create text node from buffer
    String* text_str = stringbuf_to_string(parser->foster_text_buffer);
    Item text_node = {.item = s2it(text_str)};

    if (table_pos >= 0) {
        // Insert before the table
        log_debug("html5_flush_foster_text: inserting '%s' before table at pos %d",
                  text_str->chars, table_pos);
        MarkEditor editor(parser->input);
        editor.array_insert(Item{.element = foster_parent}, table_pos, text_node);
    } else {
        // Table not found, append
        log_debug("html5_flush_foster_text: appending '%s' to foster parent", text_str->chars);
        array_append(foster_parent, text_node, parser->pool, parser->arena);
    }

    // Reset buffer
    stringbuf_reset(parser->foster_text_buffer);
    parser->foster_table_element = nullptr;
    parser->foster_parent_element = nullptr;
}

// Per WHATWG 12.2.6.1, foster parenting inserts nodes before the table
// rather than inside it when text/elements appear in table context
void html5_foster_parent_character(Html5Parser* parser, char c) {
    // Find the last table element in the stack
    Element* table_element = nullptr;
    Element* foster_parent = nullptr;
    int table_index = -1;

    for (int i = (int)parser->open_elements->length - 1; i >= 0; i--) {
        Element* el = (Element*)parser->open_elements->items[i].element;
        const char* el_tag = ((TypeElmt*)el->type)->name.str;
        if (strcmp(el_tag, "table") == 0) {
            table_element = el;
            table_index = i;
            break;
        }
    }

    if (table_element == nullptr || table_index == 0) {
        // No table found or table is first element - insert into body/html
        if (parser->open_elements->length >= 2) {
            foster_parent = (Element*)parser->open_elements->items[1].element;
        } else if (parser->open_elements->length >= 1) {
            foster_parent = (Element*)parser->open_elements->items[0].element;
        }
        if (foster_parent) {
            // Flush any existing foster text if it's for a different parent
            if (parser->foster_parent_element != nullptr &&
                parser->foster_parent_element != foster_parent) {
                html5_flush_foster_text(parser);
            }
            // Use normal text insertion since there's no table to insert before
            if (parser->pending_text_parent != nullptr &&
                parser->pending_text_parent != foster_parent) {
                html5_flush_pending_text(parser);
            }
            stringbuf_append_char(parser->text_buffer, c);
            parser->pending_text_parent = foster_parent;
        }
        return;
    }

    // Find the table's actual DOM parent
    // First try searching backwards in the open elements stack
    for (int i = table_index - 1; i >= 0; i--) {
        Element* candidate = (Element*)parser->open_elements->items[i].element;
        for (size_t j = 0; j < candidate->length; j++) {
            if (candidate->items[j].element == table_element) {
                foster_parent = candidate;
                break;
            }
        }
        if (foster_parent != nullptr) {
            break;
        }
    }

    // If not found in open_elements, search the entire DOM tree
    if (foster_parent == nullptr && parser->document != nullptr) {
        Element* search_root = nullptr;
        for (size_t i = 0; i < parser->document->length; i++) {
            TypeId type = get_type_id(parser->document->items[i]);
            if (type == LMD_TYPE_ELEMENT) {
                Element* child = parser->document->items[i].element;
                const char* child_tag = ((TypeElmt*)child->type)->name.str;
                if (strcmp(child_tag, "html") == 0) {
                    search_root = child;
                    break;
                }
            }
        }
        if (search_root != nullptr) {
            int unused_pos;
            foster_parent = find_parent_of_element(search_root, table_element, &unused_pos);
        }
    }

    // Fallback: use element before table in stack
    if (foster_parent == nullptr) {
        foster_parent = (Element*)parser->open_elements->items[table_index - 1].element;
    }

    // Flush any pending normal text
    html5_flush_pending_text(parser);

    // Check if we're continuing to buffer for the same table/parent
    if (parser->foster_table_element == table_element &&
        parser->foster_parent_element == foster_parent) {
        // Just append to existing buffer
        stringbuf_append_char(parser->foster_text_buffer, c);
        return;
    }

    // New table context - flush any previous foster text
    html5_flush_foster_text(parser);

    // Start new foster text buffer
    parser->foster_table_element = table_element;
    parser->foster_parent_element = foster_parent;
    stringbuf_append_char(parser->foster_text_buffer, c);
}

void html5_insert_comment(Html5Parser* parser, Html5Token* token) {
    // Flush any pending text before inserting comment
    html5_flush_pending_text(parser);

    // comments are stored as special element nodes with name "#comment"
    MarkBuilder builder(parser->input);

    // Get comment data - might be empty or null
    const char* comment_data = "";
    size_t comment_len = 0;
    if (token->data && token->data->chars) {
        comment_data = token->data->chars;
        comment_len = token->data->len;
    }

    // Create the comment element - empty data becomes null
    ElementBuilder elem_builder = builder.element("#comment");

    // Only set data attribute if there's actual content
    // For empty comments, we still need the data attribute but with empty value
    if (comment_len > 0) {
        elem_builder.attr("data", comment_data);
    } else {
        // For empty comment, store a single space as placeholder
        // The test expects "<!--  -->" (with space) for empty comments
        elem_builder.attr("data", "");
    }

    Element* comment = elem_builder.final().element;

    Element* parent = html5_current_node(parser);
    if (parent == nullptr) {
        parent = parser->document;
    }

    array_append(parent, Item{.element = comment}, parser->pool, parser->arena);
    log_debug("html5: inserted comment with data len=%zu", comment_len);
}

// ==================== ADOPTION AGENCY ALGORITHM ====================

// Check if tag name is a formatting element (per WHATWG spec)
bool html5_is_formatting_element(const char* tag_name) {
    static const char* formatting_elements[] = {
        "a", "b", "big", "code", "em", "font", "i", "nobr", "s", "small",
        "span", "strike", "strong", "tt", "u"
    };
    for (size_t i = 0; i < sizeof(formatting_elements)/sizeof(formatting_elements[0]); i++) {
        if (strcmp(tag_name, formatting_elements[i]) == 0) {
            return true;
        }
    }
    return false;
}

// Check if tag name is a special element (per WHATWG spec)
bool html5_is_special_element(const char* tag_name) {
    static const char* special_elements[] = {
        "address", "applet", "area", "article", "aside", "base", "basefont",
        "bgsound", "blockquote", "body", "br", "button", "caption", "center",
        "col", "colgroup", "dd", "details", "dir", "div", "dl", "dt", "embed",
        "fieldset", "figcaption", "figure", "footer", "form", "frame", "frameset",
        "h1", "h2", "h3", "h4", "h5", "h6", "head", "header", "hgroup", "hr",
        "html", "iframe", "img", "input", "keygen", "li", "link", "listing",
        "main", "marquee", "menu", "meta", "nav", "noembed", "noframes",
        "noscript", "object", "ol", "p", "param", "plaintext", "pre", "script",
        "section", "select", "source", "style", "summary", "table", "tbody",
        "td", "template", "textarea", "tfoot", "th", "thead", "title", "tr",
        "track", "ul", "wbr", "xmp"
    };
    for (size_t i = 0; i < sizeof(special_elements)/sizeof(special_elements[0]); i++) {
        if (strcmp(tag_name, special_elements[i]) == 0) {
            return true;
        }
    }
    return false;
}

// Create element for token (without inserting into tree)
// Applies SVG/MathML namespace corrections per WHATWG spec
Element* html5_create_element_for_token(Html5Parser* parser, Html5Token* token) {
    MarkBuilder builder(parser->input);

    // Check if we're in SVG namespace and need tag name correction
    const char* tag_name = token->tag_name->chars;
    bool in_svg = html5_is_in_svg_namespace(parser);

    if (in_svg) {
        // Apply SVG tag name correction (e.g., "clippath" -> "clipPath")
        tag_name = html5_lookup_svg_tag(tag_name);
    }

    ElementBuilder eb = builder.element(tag_name);

    // Copy attributes from token to element
    if (token->attributes != nullptr) {
        MapReader reader(token->attributes);
        MapReader::EntryIterator it = reader.entries();
        const char* key;
        ItemReader value;
        while (it.next(&key, &value)) {
            if (key && value.isString()) {
                // Apply SVG attribute name correction if in SVG namespace
                const char* attr_name = key;
                if (in_svg) {
                    attr_name = html5_lookup_svg_attr(key);
                }

                // Get the actual String* pointer to preserve empty strings as null
                String* str_value = value.asString();
                if (str_value) {
                    eb.attr(attr_name, Item{.item = s2it(str_value)});
                }
            }
        }
    }

    Element* elem = eb.final().element;
    return elem;
}

// Push element to active formatting list
void html5_push_active_formatting_element(Html5Parser* parser, Element* elem, Html5Token* token) {
    (void)token;  // token can be used for Noah's Ark clause, not implemented yet

    // Add to active formatting elements list
    Item item = {.element = elem};
    array_append(parser->active_formatting, item, parser->pool, parser->arena);
    log_debug("html5: added <%s> to active formatting list, size=%zu",
              ((TypeElmt*)elem->type)->name.str, parser->active_formatting->length);
}

// Push a marker onto active formatting list
void html5_push_active_formatting_marker(Html5Parser* parser) {
    Item marker = {.element = nullptr};
    array_append(parser->active_formatting, marker, parser->pool, parser->arena);
    log_debug("html5: pushed marker to active formatting list");
}

// Find formatting element in active formatting list (returns -1 if not found)
int html5_find_formatting_element(Html5Parser* parser, const char* tag_name) {
    for (int i = (int)parser->active_formatting->length - 1; i >= 0; i--) {
        Element* elem = (Element*)parser->active_formatting->items[i].element;
        if (elem == nullptr) {
            // marker - stop searching
            return -1;
        }
        const char* elem_tag = ((TypeElmt*)elem->type)->name.str;
        if (strcmp(elem_tag, tag_name) == 0) {
            return i;
        }
    }
    return -1;
}

// Find element in open elements stack (returns -1 if not found)
int html5_find_element_in_stack(Html5Parser* parser, Element* elem) {
    for (int i = (int)parser->open_elements->length - 1; i >= 0; i--) {
        if (parser->open_elements->items[i].element == elem) {
            return i;
        }
    }
    return -1;
}

// Remove element from active formatting list by index
void html5_remove_from_active_formatting(Html5Parser* parser, int index) {
    if (index < 0 || index >= (int)parser->active_formatting->length) {
        return;
    }
    // shift elements down
    for (size_t i = index; i < parser->active_formatting->length - 1; i++) {
        parser->active_formatting->items[i] = parser->active_formatting->items[i + 1];
    }
    parser->active_formatting->length--;
}

// Remove element from open elements stack by index
void html5_remove_from_stack(Html5Parser* parser, int index) {
    if (index < 0 || index >= (int)parser->open_elements->length) {
        return;
    }
    // shift elements down
    for (size_t i = index; i < parser->open_elements->length - 1; i++) {
        parser->open_elements->items[i] = parser->open_elements->items[i + 1];
    }
    parser->open_elements->length--;
}

// Helper: insert element at specified position in array
// Uses array_append to ensure capacity, then shifts elements
static void array_insert_at(Array* arr, int position, Item item, Pool* pool, Arena* arena) {
    // First, append a dummy to ensure capacity
    array_append(arr, item, pool, arena);

    // Now shift elements from the end down to position
    for (int64_t i = arr->length - 1; i > position; i--) {
        arr->items[i] = arr->items[i - 1];
    }

    // Insert the item at position
    arr->items[position] = item;
}

// Helper: remove specific child element from parent
static void remove_element_child(Element* parent, Element* child) {
    for (int64_t i = 0; i < parent->length; i++) {
        if (parent->items[i].element == child) {
            // Shift remaining elements down
            for (int64_t j = i; j < parent->length - 1; j++) {
                parent->items[j] = parent->items[j + 1];
            }
            parent->length--;
            return;
        }
    }
}

// Helper: move all children from one element to another
static void reparent_children(Html5Parser* parser, Element* from, Element* to) {
    for (int64_t i = 0; i < from->length; i++) {
        Item child = from->items[i];
        array_append((Array*)to, child, parser->pool, parser->arena);
    }
    from->length = 0;
}

// The Adoption Agency Algorithm
// Implements WHATWG spec section 12.2.6.4.7
void html5_run_adoption_agency(Html5Parser* parser, Html5Token* token) {
    const char* subject = token->tag_name->chars;
    log_debug("html5: running adoption agency for </%s>", subject);

    // Note: We don't flush text at the start. We only flush when we're about to
    // restructure the tree. This allows text before an unmatched end tag like
    // </i> to merge with text after it.

    // Outer loop limit
    int outer_loop_counter = 0;

    while (outer_loop_counter < 8) {
        outer_loop_counter++;

        // Step 1: Find formatting element
        int formatting_element_idx = html5_find_formatting_element(parser, subject);
        if (formatting_element_idx < 0) {
            // No formatting element found - treat as "any other end tag"
            log_debug("html5: AAA - no formatting element found for </%s>", subject);

            // Generic end tag handling
            for (int i = (int)parser->open_elements->length - 1; i >= 0; i--) {
                Element* node = (Element*)parser->open_elements->items[i].element;
                const char* node_tag = ((TypeElmt*)node->type)->name.str;

                if (strcmp(node_tag, subject) == 0) {
                    // Found matching element - flush text and process
                    html5_flush_pending_text(parser);
                    html5_generate_implied_end_tags_except(parser, subject);
                    while ((int)parser->open_elements->length > i) {
                        html5_pop_element(parser);
                    }
                    return;
                }

                if (html5_is_special_element(node_tag)) {
                    // Hit special element - ignore token, don't flush text
                    return;
                }
            }
            return;
        }

        // We found a formatting element
        Element* formatting_element = (Element*)parser->active_formatting->items[formatting_element_idx].element;

        // Step 2: If formatting element not in stack of open elements
        int fe_stack_idx = html5_find_element_in_stack(parser, formatting_element);
        if (fe_stack_idx < 0) {
            log_debug("html5: AAA - formatting element not in stack, removing from active list");
            html5_remove_from_active_formatting(parser, formatting_element_idx);
            return;
        }

        // Step 3: If formatting element is not in scope, parse error
        if (!html5_has_element_in_scope(parser, subject)) {
            log_error("html5: AAA - formatting element not in scope");
            return;
        }

        // Step 4: If formatting element is not current node, parse error (continue)
        if (formatting_element != html5_current_node(parser)) {
            log_debug("html5: AAA - formatting element is not current node (parse error, continuing)");
        }

        // Step 5: Find furthest block
        Element* furthest_block = nullptr;
        int furthest_block_idx = -1;

        for (int i = fe_stack_idx + 1; i < (int)parser->open_elements->length; i++) {
            Element* node = (Element*)parser->open_elements->items[i].element;
            const char* node_tag = ((TypeElmt*)node->type)->name.str;
            if (html5_is_special_element(node_tag)) {
                furthest_block = node;
                furthest_block_idx = i;
                break;
            }
        }

        // Step 6: If no furthest block, pop until formatting element is popped
        if (furthest_block == nullptr) {
            log_debug("html5: AAA - no furthest block, popping to formatting element");
            while (parser->open_elements->length > 0) {
                Element* popped = html5_pop_element(parser);
                if (popped == formatting_element) {
                    break;
                }
            }
            html5_remove_from_active_formatting(parser, formatting_element_idx);
            return;
        }

        // Check if furthest block is a form-associated element
        // In standards mode (not quirks), form controls create a barrier for AAA
        const char* fb_tag = ((TypeElmt*)furthest_block->type)->name.str;
        log_debug("html5: AAA - furthest block is <%s>", fb_tag);
        if (!parser->quirks_mode &&
            (strcmp(fb_tag, "button") == 0 || strcmp(fb_tag, "input") == 0 ||
             strcmp(fb_tag, "select") == 0 || strcmp(fb_tag, "textarea") == 0 ||
             strcmp(fb_tag, "keygen") == 0 || strcmp(fb_tag, "output") == 0)) {
            log_debug("html5: AAA - furthest block is form control in standards mode, ignoring end tag");
            return;  // Don't restructure across form controls - don't flush text either
        }

        // If we reach here, we're going to restructure the tree - flush pending text now
        html5_flush_pending_text(parser);

        // Step 7: Get common ancestor (element before formatting element in stack)
        Element* common_ancestor = (Element*)parser->open_elements->items[fe_stack_idx - 1].element;
        log_debug("html5: AAA - common ancestor: <%s>, furthest block: <%s>",
                  ((TypeElmt*)common_ancestor->type)->name.str,
                  ((TypeElmt*)furthest_block->type)->name.str);

        // Step 8: Remember bookmark (position in active formatting list)
        int bookmark = formatting_element_idx;

        // Step 9: Initialize node and last node
        Element* node = furthest_block;
        int node_idx = furthest_block_idx;
        Element* last_node = furthest_block;

        // Track the tree parent of last_node (for step 11)
        // Initially, furthest_block's parent in the tree is the element
        // immediately before it in the stack (at node_idx - 1)
        Element* last_node_parent = (Element*)parser->open_elements->items[node_idx - 1].element;

        // Step 10: Inner loop
        int inner_loop_counter = 0;

        while (true) {
            inner_loop_counter++;

            // Step 10.1: Move node to previous in stack
            node_idx--;
            node = (Element*)parser->open_elements->items[node_idx].element;

            // Step 10.2: If node is formatting element, exit inner loop
            if (node == formatting_element) {
                break;
            }

            // Step 10.3: If inner loop > 3 and node is in active formatting list
            int node_active_idx = -1;
            for (int i = 0; i < (int)parser->active_formatting->length; i++) {
                if (parser->active_formatting->items[i].element == node) {
                    node_active_idx = i;
                    break;
                }
            }

            if (inner_loop_counter > 3 && node_active_idx >= 0) {
                html5_remove_from_active_formatting(parser, node_active_idx);
                if (node_active_idx < bookmark) {
                    bookmark--;
                }
                node_active_idx = -1;
            }

            // Step 10.4: If node is not in active formatting list, remove from stack
            if (node_active_idx < 0) {
                html5_remove_from_stack(parser, node_idx);
                // Note: last_node's parent in the tree is still "node" since
                // we're just removing from stack, not from tree
                // But we need to track this for step 11
                // Actually, when we skip node, the parent becomes the next node
                // we'll process, so we update last_node_parent to node's parent
                // which is at node_idx - 1 (after removal, indices shift)
                // This is complex - let's just search for parent in step 11
                continue;
            }

            // Step 10.5: Create a new element with same tag name
            MarkBuilder builder(parser->input);
            const char* node_tag = ((TypeElmt*)node->type)->name.str;
            Element* new_element = builder.element(node_tag).final().element;

            // Replace in active formatting list
            parser->active_formatting->items[node_active_idx].element = new_element;

            // Replace in stack of open elements
            parser->open_elements->items[node_idx].element = new_element;

            node = new_element;

            // Step 10.6: If last node is furthest block, update bookmark
            if (last_node == furthest_block) {
                bookmark = node_active_idx + 1;
            }

            // Step 10.7: Append last node to node
            // Remove from old parent first
            if (last_node_parent != nullptr) {
                remove_element_child(last_node_parent, last_node);
            }
            array_append((Array*)node, Item{.element = last_node}, parser->pool, parser->arena);

            // Update parent tracking
            last_node_parent = node;

            // Step 10.8: Set last node to node
            last_node = node;
        }

        // Step 11: Insert last_node at appropriate place
        // First, remove last_node from its current parent
        // This is the element that contains last_node in the tree
        if (last_node_parent != nullptr) {
            remove_element_child(last_node_parent, last_node);
        }

        // Now insert into common_ancestor
        array_append((Array*)common_ancestor, Item{.element = last_node}, parser->pool, parser->arena);

        // Step 12: Create new element with same tag as formatting element
        MarkBuilder builder(parser->input);
        Element* new_formatting_element = builder.element(subject).final().element;

        // Step 13: Take all children of furthest block and append to new element
        reparent_children(parser, furthest_block, new_formatting_element);

        // Step 14: Append new element to furthest block
        array_append((Array*)furthest_block, Item{.element = new_formatting_element},
                     parser->pool, parser->arena);

        // Step 15: Remove formatting element from active list, insert new at bookmark
        html5_remove_from_active_formatting(parser, formatting_element_idx);
        if (bookmark > (int)parser->active_formatting->length) {
            bookmark = (int)parser->active_formatting->length;
        }
        // Insert at bookmark position using our helper
        Item new_fe_item = {.element = new_formatting_element};
        array_insert_at((Array*)parser->active_formatting, bookmark, new_fe_item,
                        parser->pool, parser->arena);

        // Step 16: Remove formatting element from stack, insert new after furthest block
        html5_remove_from_stack(parser, fe_stack_idx);

        // Find furthest block in stack (its index may have shifted)
        int fb_new_idx = html5_find_element_in_stack(parser, furthest_block);
        if (fb_new_idx >= 0) {
            // Insert new formatting element after furthest block
            Item new_fe_item2 = {.element = new_formatting_element};
            array_insert_at((Array*)parser->open_elements, fb_new_idx + 1, new_fe_item2,
                            parser->pool, parser->arena);
        }

        log_debug("html5: AAA iteration complete for </%s>", subject);
    }

    log_debug("html5: AAA completed after %d iterations", outer_loop_counter);
}
