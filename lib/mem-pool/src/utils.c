#include <stdalign.h>
#include <stddef.h>

// For C99 compatibility, define max_align_t if not available
#ifndef __STDC_VERSION__
#define __STDC_VERSION__ 199901L
#endif

#if __STDC_VERSION__ < 201112L
typedef union {
    long long ll;
    long double ld;
} max_align_t;
#endif

size_t mem_align(size_t size)
{
    size_t align = alignof(max_align_t);

    if (size % align) {
        return size + (align - size % align);
    }

    return size;
}

