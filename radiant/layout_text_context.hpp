#pragma once

#include <stddef.h>

// byte range of the shaping run adjoining the selected edge of a UTF-8 slice.
void layout_text_shaping_context_slice(const char* source, size_t length,
    bool forward, bool cursive, size_t* first, size_t* last);
