#include "utf.h"
#include <utf8proc.h>

char* utf8_normalize(const char* text, size_t length, UtfNormalizationForm form, size_t* out_length) {
    static const utf8proc_option_t forms[] = {
        UTF8PROC_COMPOSE, UTF8PROC_DECOMPOSE,
        UTF8PROC_COMPOSE | UTF8PROC_COMPAT, UTF8PROC_DECOMPOSE | UTF8PROC_COMPAT,
        UTF8PROC_COMPOSE | UTF8PROC_CASEFOLD
    };
    if (out_length) *out_length = 0;
    if (!text || !length || length > PTRDIFF_MAX || (unsigned)form >= sizeof(forms) / sizeof(forms[0]))
        return NULL;
    utf8proc_uint8_t* result = NULL;
    utf8proc_ssize_t bytes = utf8proc_map((const utf8proc_uint8_t*)text,
        (utf8proc_ssize_t)length, &result, (utf8proc_option_t)(UTF8PROC_STABLE | forms[form]));
    if (bytes < 0) return NULL;
    if (out_length) *out_length = (size_t)bytes;
    return (char*)result;
}
