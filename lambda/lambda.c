#ifndef JIT_MODE
#include "lambda.h"
#endif
#include <stdlib.h>
#include <stdarg.h>

Array array(int count, ...) {
    if (count <= 0) { return NULL; }
    va_list args;
    va_start(args, count);
    Array arr = malloc(count * sizeof(Item));
    for (int i = 0; i < count; i++) {
        arr[i] = va_arg(args, Item);
    }
    va_end(args);
    return arr;
}

long* array_long(int count, ...) {
    long* arr = malloc(count * sizeof(long));
    if (count <= 0) { return NULL; }
    va_list args;
    va_start(args, count);
    for (int i = 0; i < count; i++) {
        arr[i] = va_arg(args, long);
    }       
    va_end(args);
    return arr;
}