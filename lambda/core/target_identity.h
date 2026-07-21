#ifndef LAMBDA_CORE_TARGET_IDENTITY_H
#define LAMBDA_CORE_TARGET_IDENTITY_H

typedef struct Target Target;

#ifdef __cplusplus
extern "C" {
#endif

// Target identity is a value comparison over its normalized hash. It does not
// acquire, resolve, or free a resource, so core supplies it below lambda-io.
bool target_equal(Target* first, Target* second);

#ifdef __cplusplus
}
#endif

#endif
