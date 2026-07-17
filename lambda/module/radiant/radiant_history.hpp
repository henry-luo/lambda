#pragma once

#include "../../lambda-data.hpp"

#ifdef __cplusplus
#define RADIANT_HISTORY_API extern "C"
#else
#define RADIANT_HISTORY_API extern
#endif

struct DomDocument;

typedef struct RadiantHistoryTraversal {
    Item state;
    const char* old_url;
    const char* new_url;
    bool hash_changed;
} RadiantHistoryTraversal;

RADIANT_HISTORY_API bool radiant_history_initialize(DomDocument* document);
RADIANT_HISTORY_API int radiant_history_length(DomDocument* document);
RADIANT_HISTORY_API Item radiant_history_state(DomDocument* document);
RADIANT_HISTORY_API const char* radiant_history_scroll_restoration(DomDocument* document);
RADIANT_HISTORY_API void radiant_history_set_scroll_restoration(
    DomDocument* document, const char* value);
RADIANT_HISTORY_API bool radiant_history_push_state(
    DomDocument* document, Item cloned_state, const char* url_text);
RADIANT_HISTORY_API bool radiant_history_replace_state(
    DomDocument* document, Item cloned_state, const char* url_text);
RADIANT_HISTORY_API bool radiant_history_go(
    DomDocument* document, int delta, RadiantHistoryTraversal* traversal);
RADIANT_HISTORY_API bool radiant_history_set_location(
    DomDocument* document, const char* url_text, RadiantHistoryTraversal* traversal);
