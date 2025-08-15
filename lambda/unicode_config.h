#ifndef LAMBDA_UNICODE_CONFIG_H
#define LAMBDA_UNICODE_CONFIG_H

// Lambda Unicode Support Configuration
// This file defines the Unicode support level for Lambda engine

// Unicode support levels
#define LAMBDA_UNICODE_NONE     0  // ASCII-only comparison (~0KB overhead)
#define LAMBDA_UNICODE_MINIMAL  1  // Basic Unicode support (~200KB overhead)  
#define LAMBDA_UNICODE_COMPACT  2  // Stripped ICU (~2-4MB overhead)
#define LAMBDA_UNICODE_FULL     3  // Full ICU (~8-12MB overhead)

// Set default Unicode level (can be overridden at compile time)
#ifndef LAMBDA_UNICODE_LEVEL
    #define LAMBDA_UNICODE_LEVEL LAMBDA_UNICODE_COMPACT
#endif

// Feature flags based on Unicode level
#if LAMBDA_UNICODE_LEVEL >= LAMBDA_UNICODE_COMPACT
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

// ICU configuration for compact build
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
