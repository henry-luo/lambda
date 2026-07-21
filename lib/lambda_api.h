#ifndef LAMBDA_API_H
#define LAMBDA_API_H

// Module API annotations are source-contract markers today.  Static archives
// do not need symbol exporting, but keeping the annotation at each public
// declaration lets the validation DSOs adopt hidden visibility without
// changing their headers again.
#if defined(_WIN32)
#if defined(LAMBDA_BUILD_SHARED)
#define LAMBDA_API __declspec(dllexport)
#elif defined(LAMBDA_USE_SHARED)
#define LAMBDA_API __declspec(dllimport)
#else
#define LAMBDA_API
#endif
#elif defined(__GNUC__) || defined(__clang__)
#define LAMBDA_API __attribute__((visibility("default")))
#else
#define LAMBDA_API
#endif

#define LAMBDA_LIB_API LAMBDA_API
#define LAMBDA_CORE_API LAMBDA_API
#define LAMBDA_IO_API LAMBDA_API
#define LAMBDA_RT_API LAMBDA_API
#define RADIANT_API LAMBDA_API

#endif
