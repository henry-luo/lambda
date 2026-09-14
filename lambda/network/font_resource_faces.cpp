#include "font_resource_faces.h"
#include "../../lib/mem.h"
#include "../../lib/str.h"
#include "../../lib/intrusive_queue.h"

typedef struct FontResourceFaceNode {
    // link is first so this domain record can stay allocation/lifecycle-owned here.
    IntrusiveQueueNode link;
    CssFontFaceDescriptor* descriptor;
} FontResourceFaceNode;

struct FontResourceFaceList {
    IntrusiveQueue queue;
};

static CssFontFaceDescriptor* clone_font_resource_face(
        const CssFontFaceDescriptor* source) {
    if (!source) return NULL;

    CssFontFaceDescriptor* clone = (CssFontFaceDescriptor*)mem_calloc(
        1, sizeof(CssFontFaceDescriptor), MEM_CAT_NETWORK);
    if (!clone) return NULL;

    clone->family_name = source->family_name
        ? mem_strdup(source->family_name, MEM_CAT_NETWORK) : NULL;
    clone->font_style = source->font_style;
    clone->font_weight = source->font_weight;
    clone->font_display = source->font_display;
    return clone;
}

static bool font_resource_faces_match(const CssFontFaceDescriptor* left,
                                      const CssFontFaceDescriptor* right) {
    if (!left || !right || !left->family_name || !right->family_name) return false;
    return str_icmp_cstr(left->family_name, right->family_name) == 0 &&
           left->font_style == right->font_style &&
           left->font_weight == right->font_weight;
}

bool font_resource_face_list_add_unique(FontResourceFaceList** list,
                                        const CssFontFaceDescriptor* descriptor) {
    if (!list || !descriptor) return false;
    for (IntrusiveQueueNode* link = *list ? (*list)->queue.first : NULL;
         link; link = link->next) {
        FontResourceFaceNode* node = (FontResourceFaceNode*)link;
        if (font_resource_faces_match(node->descriptor, descriptor)) return false;
    }

    CssFontFaceDescriptor* clone = clone_font_resource_face(descriptor);
    if (!clone) return false;
    FontResourceFaceNode* node = (FontResourceFaceNode*)mem_calloc(
        1, sizeof(FontResourceFaceNode), MEM_CAT_NETWORK);
    if (!node) {
        css_font_face_descriptor_free(clone);
        return false;
    }
    node->descriptor = clone;

    if (!*list) {
        *list = (FontResourceFaceList*)mem_calloc(
            1, sizeof(FontResourceFaceList), MEM_CAT_NETWORK);
        if (!*list) {
            css_font_face_descriptor_free(clone);
            mem_free(node);
            return false;
        }
    }

    // One variable-font URL may implement several CSS faces; URL dedup must retain each descriptor.
    intrusive_queue_push(&(*list)->queue, &node->link);
    return true;
}

int font_resource_face_list_count(const FontResourceFaceList* list) {
    return list ? (int)list->queue.count : 0;
}

void font_resource_face_list_for_each(const FontResourceFaceList* list,
                                      FontResourceFaceVisitor visitor,
                                      void* user_data) {
    if (!list || !visitor) return;
    for (IntrusiveQueueNode* link = list->queue.first; link; link = link->next) {
        const FontResourceFaceNode* node = (const FontResourceFaceNode*)link;
        visitor(node->descriptor, user_data);
    }
}

void font_resource_face_list_destroy(FontResourceFaceList* list) {
    if (!list) return;
    IntrusiveQueueNode* link = NULL;
    while ((link = intrusive_queue_pop(&list->queue))) {
        FontResourceFaceNode* node = (FontResourceFaceNode*)link;
        css_font_face_descriptor_free(node->descriptor);
        mem_free(node);
    }
    mem_free(list);
}
