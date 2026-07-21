// Runtime decimal results own GC-backed Decimal headers. The static data
// archive compiles only representation, formatting, and arena constructors.
#define LAMBDA_DECIMAL_RUNTIME_IMPLEMENTATION
#include "../lambda-decimal.cpp"
