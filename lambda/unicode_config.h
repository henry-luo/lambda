#ifndef LAMBDA_UNICODE_CONFIG_H
#define LAMBDA_UNICODE_CONFIG_H

// Lambda Unicode Support Configuration
// This file defines the Unicode support level for Lambda engine

// Unicode support levels
#define LAMBDA_UNICODE_NONE     0  // ASCII-only comparison (~0KB overhead)
#define LAMBDA_UNICODE_MINIMAL  1  // Basic Unicode support (~200KB overhead)  
#define LAMBDA_UNICODE_UTF8PROC 2  // utf8proc Unicode support (~350KB overhead)
#define LAMBDA_UNICODE_COMPACT  3  // Stripped ICU (~2-4MB overhead) - deprecated
#define LAMBDA_UNICODE_FULL     4  // Full ICU (~8-12MB overhead) - deprecated

// Set default Unicode level (can be overridden at compile time)
#ifndef LAMBDA_UNICODE_LEVEL
    #define LAMBDA_UNICODE_LEVEL LAMBDA_UNICODE_UTF8PROC
#endif

// Feature flags based on Unicode level
#if LAMBDA_UNICODE_LEVEL >= LAMBDA_UNICODE_UTF8PROC
    #define LAMBDA_UTF8PROC_SUPPORT 1
    #define LAMBDA_UNICODE_COLLATION 1
    #define LAMBDA_UNICODE_NORMALIZATION 1
    #define LAMBDA_ASCII_FAST_PATH 1
#elif LAMBDA_UNICODE_LEVEL >= LAMBDA_UNICODE_COMPACT
    #define LAMBDA_ICU_SUPPORT 1
    #define LAMBDA_UNICODE_COLLATION 1
    #define LAMBDA_UNICODE_NORMALIZATION 1
    #define LAMBDA_ASCII_FAST_PATH 1
#elif LAMBDA_UNICODE_LEVEL == LAMBDA_UNICODE_MINIMAL
    #define LAMBDA_MINIMAL_UNICODE 1
    #define LAMBDA_UNICODE_COLLATION 1
    #define LAMBDA_ASCII_FAST_PATH 1
#else
    #define LAMBDA_ASCII_FAST_PATH 1
#endif

// utf8proc configuration for optimal build
#ifdef LAMBDA_UTF8PROC_SUPPORT
    #define UTF8PROC_STATIC 1
    // Compile-time options for utf8proc optimization
    // (utf8proc is already quite compact, no special flags needed)
#endif

// ICU configuration for compact build (deprecated)
#ifdef LAMBDA_ICU_SUPPORT
    #define U_STATIC_IMPLEMENTATION 1
    #define U_DISABLE_RENAMING 1
    #define UCONFIG_NO_SERVICE 1
    #define UCONFIG_NO_REGULAR_EXPRESSIONS 1
    #define UCONFIG_NO_FORMATTING 1
    #define UCONFIG_NO_TRANSLITERATION 1
    #define UCONFIG_NO_BREAK_ITERATION 1
    #define UCONFIG_NO_IDNA 1
    #define UCONFIG_NO_LEGACY_CONVERSION 1
#endif

#endif // LAMBDA_UNICODE_CONFIG_H
