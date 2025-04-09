#include <stdio.h>
#include <stdbool.h>
#define null 0
typedef void* Item;
typedef Item* Array;
Array array(int count, ...);
long* array_long(int count, ...);