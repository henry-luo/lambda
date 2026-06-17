#ifndef RADIANT_EDITING_RICH_TRANSACTION_HPP
#define RADIANT_EDITING_RICH_TRANSACTION_HPP

#include "editing.hpp"
#include "editing_intent.hpp"
#include <stdint.h>

struct DocState;
struct DomNode;
struct DomText;

typedef void (*EditingRichMutationLogFn)(DocState* state,
                                         const EditingSurface* surface,
                                         const EditingIntent* intent,
                                         const char* operation,
                                         uint32_t old_len,
                                         uint32_t new_len,
                                         uint32_t selection_start,
                                         uint32_t selection_end,
                                         void* user);

DomText* editing_rich_find_text_descendant(DomNode* node, bool last);
bool editing_rich_is_composition_intent(const EditingIntent* intent);

bool editing_rich_default_replace(DocState* state,
                                  const EditingIntent* intent,
                                  View* fallback_view,
                                  int fallback_offset,
                                  EditingRichMutationLogFn log_mutation,
                                  void* log_user);

bool editing_rich_default_format(DocState* state,
                                 const EditingSurface* surface,
                                 const EditingIntent* intent,
                                 EditingRichMutationLogFn log_mutation,
                                 void* log_user);

bool editing_rich_default_format_block(DocState* state,
                                       const EditingSurface* surface,
                                       const EditingIntent* intent,
                                       EditingRichMutationLogFn log_mutation,
                                       void* log_user);

#endif // RADIANT_EDITING_RICH_TRANSACTION_HPP
