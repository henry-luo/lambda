#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// D7.5.2: the broker classifies acquired objects; lexical paths are never grants.
typedef uint64_t SecurityOperations;
enum {
    SECURITY_READ = 1ULL << 0,
    SECURITY_METADATA = 1ULL << 1,
    SECURITY_ENUMERATE = 1ULL << 2,
    SECURITY_CREATE = 1ULL << 3,
    SECURITY_REPLACE = 1ULL << 4,
    SECURITY_APPEND = 1ULL << 5,
    SECURITY_DELETE = 1ULL << 6,
    SECURITY_RENAME = 1ULL << 7,
    SECURITY_WATCH = 1ULL << 8,
    SECURITY_LOAD_SOURCE = 1ULL << 9,
    SECURITY_CONNECT = 1ULL << 10,
    SECURITY_LISTEN = 1ULL << 11,
    SECURITY_INVOKE = 1ULL << 12,
    SECURITY_ALL_OPERATIONS = (1ULL << 13) - 1
};

typedef enum SecurityScope {
    SECURITY_SCOPE_ENTRY,
    SECURITY_SCOPE_PROJECT,
    SECURITY_SCOPE_SHIPPED,
    SECURITY_SCOPE_PRIVATE,
    SECURITY_SCOPE_SELECTED,
    SECURITY_SCOPE_PROTECTED
} SecurityScope;

typedef enum SecurityObjectKind {
    SECURITY_OBJECT_FILE,
    SECURITY_OBJECT_DIRECTORY,
    SECURITY_OBJECT_NEW_FILE,
    SECURITY_OBJECT_NEW_DIRECTORY,
    SECURITY_OBJECT_ENDPOINT,
    SECURITY_OBJECT_SERVICE
} SecurityObjectKind;

typedef enum SecurityDecision {
    SECURITY_ALLOWED,
    SECURITY_APPROVAL_REQUIRED,
    SECURITY_DENIED,
    SECURITY_REVOKED,
    SECURITY_INVALID_REQUEST,
    SECURITY_LIMIT_EXCEEDED
} SecurityDecision;

typedef struct SecurityResourceRef {
    uint64_t session;
    uint64_t serial;
} SecurityResourceRef;

typedef struct SecurityResourceSpec {
    // retain unknown wire/schema values until validation; invalid C++ enums are UB
    int32_t scope;
    int32_t kind;
    SecurityOperations ceiling;
    bool admitted_source;
} SecurityResourceSpec;

typedef struct SecurityPolicyConfig {
    // assigned by the trusted host; session must be unique across live/reused workers
    uint64_t principal;
    uint64_t session;
    SecurityOperations ceiling;
    uint32_t max_resources;
    uint32_t max_grants;
} SecurityPolicyConfig;

typedef struct SecurityRequest {
    SecurityResourceRef resource;
    uint64_t generation;
    // exactly one operation; adapters split compound operations into checked steps
    SecurityOperations operation;
    // trusted host clock, in the same units as grant expiry; never a worker timestamp
    uint64_t now;
} SecurityRequest;

typedef struct SecurityPolicy SecurityPolicy;

SecurityPolicy* security_policy_create(const SecurityPolicyConfig* config);
void security_policy_destroy(SecurityPolicy* policy);
uint64_t security_policy_generation(SecurityPolicy* policy);
SecurityDecision security_policy_check(SecurityPolicy* policy, const SecurityRequest* request);

// Host-only management API. These functions do not authenticate IPC callers.
// Resources require separate broker acquisition, alias checks and object/version binding.
// Side-effect-free namespace preflight for selecting an admitted root. This
// never authorizes acquisition/delivery or creates a resource/grant serial.
SecurityDecision security_policy_scope_check(SecurityPolicy* policy,
    const SecurityResourceSpec* spec, SecurityOperations selected_operations,
    SecurityOperations operation);
SecurityDecision security_policy_admit_resource(SecurityPolicy* policy,
    const SecurityResourceSpec* spec, SecurityResourceRef* out_resource);
SecurityDecision security_policy_grant(SecurityPolicy* policy, SecurityResourceRef resource,
    SecurityOperations operations, uint64_t expires_at, uint64_t* out_grant);
bool security_policy_revoke_grant(SecurityPolicy* policy, uint64_t grant);
bool security_policy_revoke_resource(SecurityPolicy* policy, SecurityResourceRef resource);
// Close a lease without changing unrelated authority; issued serials are never reused.
bool security_policy_release_resource(SecurityPolicy* policy, SecurityResourceRef resource);
bool security_policy_restrict(SecurityPolicy* policy, SecurityOperations ceiling);
const char* security_decision_name(int32_t decision);

#ifdef __cplusplus
}
#endif
