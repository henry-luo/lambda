#ifndef FONT_RESOURCE_FACES_H
#define FONT_RESOURCE_FACES_H

#include "../input/css/css_font_face.hpp"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FontResourceFaceList FontResourceFaceList;
typedef void (*FontResourceFaceVisitor)(const CssFontFaceDescriptor* descriptor,
                                        void* user_data);

bool font_resource_face_list_add_unique(FontResourceFaceList** list,
                                        const CssFontFaceDescriptor* descriptor);
int font_resource_face_list_count(const FontResourceFaceList* list);
void font_resource_face_list_for_each(const FontResourceFaceList* list,
                                      FontResourceFaceVisitor visitor,
                                      void* user_data);
void font_resource_face_list_destroy(FontResourceFaceList* list);

#ifdef __cplusplus
}
#endif

#endif
