#ifndef LAMBDA_SANITIZER_H
#define LAMBDA_SANITIZER_H

#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define LAMBDA_ADDRESS_SANITIZER 1
#endif
#endif
#ifndef LAMBDA_ADDRESS_SANITIZER
#if defined(__SANITIZE_ADDRESS__)
#define LAMBDA_ADDRESS_SANITIZER 1
#else
#define LAMBDA_ADDRESS_SANITIZER 0
#endif
#endif

#if LAMBDA_ADDRESS_SANITIZER
#include <sanitizer/asan_interface.h>
#include <sanitizer/common_interface_defs.h>
#endif
#endif
