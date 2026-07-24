// The core path representation is Input-owned. Filesystem iteration and
// existence checks allocate/evaluate at runtime, so compile that section in
// lambda-rt without duplicating the representation implementation.
#define LAMBDA_PATH_RUNTIME_IMPLEMENTATION
#include "../core/path.c"
