#ifndef LAMBDA_RESOURCE_POLICY_H
#define LAMBDA_RESOURCE_POLICY_H
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
// dependency admission precedes source/cache acquisition; the top-level source is explicit.
typedef enum InputResourcePolicy {
    INPUT_RESOURCE_ALLOW_NETWORK = 0,
    INPUT_RESOURCE_LOCAL_ONLY
} InputResourcePolicy;
bool input_resource_policy_admits(InputResourcePolicy policy, const char* resolved_source);
#ifdef __cplusplus
}
#endif
#endif
