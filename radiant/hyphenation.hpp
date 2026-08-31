#ifndef RADIANT_HYPHENATION_HPP
#define RADIANT_HYPHENATION_HPP

#include <stddef.h>

// returns whether the bundled resource matches the declared content language.
bool layout_hyphenation_en_us_language(const char* lang);

// returns the next source-text break offset after `after` or SIZE_MAX.
size_t layout_hyphenation_en_us_next_break(const char* word, size_t length,
                                           size_t after);

#endif
