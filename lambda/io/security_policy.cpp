#include "security_policy.h"
#include "../../lib/arraylist.h"
#include "../../lib/mem.h"
#include <pthread.h>
#include <limits.h>

struct SecurityResource {
    SecurityResourceSpec spec;
    uint64_t serial;
    bool revoked;
};

struct SecurityGrant {
    SecurityResourceRef resource;
    SecurityOperations operations;
    uint64_t expires_at;
    bool revoked;
    uint64_t serial;
};

struct SecurityPolicy {
    SecurityPolicyConfig config;
    uint64_t generation;
    uint64_t next_resource;
    uint64_t next_grant;
    ArrayList* resources;
    ArrayList* grants;
    pthread_mutex_t mutex;
};

static bool security_operations_valid(SecurityOperations operations) {
    return !(operations & ~((SecurityOperations)SECURITY_ALL_OPERATIONS));
}

static SecurityOperations security_object_operations(const SecurityResourceSpec* spec) {
    const SecurityOperations mutations = SECURITY_REPLACE | SECURITY_APPEND |
        SECURITY_DELETE | SECURITY_RENAME;
    switch (spec->kind) {
        case SECURITY_OBJECT_FILE:
            return SECURITY_READ | SECURITY_METADATA | mutations | SECURITY_WATCH |
                (spec->admitted_source ? SECURITY_LOAD_SOURCE : 0);
        case SECURITY_OBJECT_DIRECTORY:
            return SECURITY_METADATA | SECURITY_ENUMERATE | SECURITY_DELETE |
                SECURITY_RENAME | SECURITY_WATCH;
        case SECURITY_OBJECT_NEW_FILE:
        case SECURITY_OBJECT_NEW_DIRECTORY:
            return SECURITY_METADATA | SECURITY_CREATE;
        case SECURITY_OBJECT_ENDPOINT:
            return SECURITY_CONNECT | SECURITY_LISTEN;
        case SECURITY_OBJECT_SERVICE:
            return SECURITY_INVOKE;
    }
    return 0;
}

static SecurityResource* security_resource_locked(SecurityPolicy* policy,
        SecurityResourceRef ref) {
    if (ref.session != policy->config.session || !ref.serial) return nullptr;
    for (int i = 0; i < policy->resources->length; i++) {
        SecurityResource* resource = (SecurityResource*)policy->resources->data[i];
        if (resource->serial == ref.serial) return resource;
    }
    return nullptr;
}

static SecurityDecision security_missing_resource_locked(SecurityPolicy* policy, SecurityResourceRef ref) {
    return ref.session == policy->config.session && ref.serial && ref.serial < policy->next_resource
        ? SECURITY_REVOKED : SECURITY_INVALID_REQUEST;
}

static SecurityOperations security_defaults(const SecurityResourceSpec* spec) {
    if (spec->kind == SECURITY_OBJECT_ENDPOINT || spec->kind == SECURITY_OBJECT_SERVICE)
        return 0;
    switch (spec->scope) {
        case SECURITY_SCOPE_ENTRY:
        case SECURITY_SCOPE_SHIPPED:
            return SECURITY_READ | SECURITY_METADATA | SECURITY_LOAD_SOURCE;
        case SECURITY_SCOPE_PROJECT:
            // D11.2.1–D11.2.2: root reads/creation do not grant destructive mutations.
            return SECURITY_READ | SECURITY_METADATA | SECURITY_ENUMERATE |
                SECURITY_LOAD_SOURCE | SECURITY_CREATE;
        case SECURITY_SCOPE_PRIVATE:
            return SECURITY_READ | SECURITY_METADATA | SECURITY_ENUMERATE |
                SECURITY_CREATE | SECURITY_REPLACE | SECURITY_APPEND |
                SECURITY_DELETE | SECURITY_RENAME | SECURITY_WATCH;
        default:
            return 0;
    }
}

static bool security_scope_permits(const SecurityResourceSpec* spec,
        SecurityOperations operations) {
    if (spec->scope == SECURITY_SCOPE_PROTECTED) return false;
    // managed code images and an admitted loose entry are read/source-only
    if ((spec->scope == SECURITY_SCOPE_SHIPPED || spec->scope == SECURITY_SCOPE_ENTRY) &&
        (operations & ~(SECURITY_READ | SECURITY_METADATA | SECURITY_LOAD_SOURCE))) return false;
    return !(operations & ~spec->ceiling) &&
        !(operations & ~security_object_operations(spec));
}

static bool security_resource_spec_valid(const SecurityResourceSpec* spec) {
    return spec && spec->scope >= SECURITY_SCOPE_ENTRY && spec->scope <= SECURITY_SCOPE_PROTECTED &&
        security_operations_valid(spec->ceiling) && security_object_operations(spec) &&
        (!spec->admitted_source || spec->kind == SECURITY_OBJECT_FILE) &&
        ((spec->scope != SECURITY_SCOPE_ENTRY && spec->scope != SECURITY_SCOPE_SHIPPED) ||
            spec->kind == SECURITY_OBJECT_FILE || spec->kind == SECURITY_OBJECT_DIRECTORY);
}

static SecurityDecision security_scope_decision(const SecurityResourceSpec* spec,
        SecurityOperations ceiling, SecurityOperations grants, SecurityOperations operation) {
    if (operation & ~ceiling) return SECURITY_DENIED;
    // Creation never truncates an existing project object, including preflight.
    if (operation == SECURITY_CREATE && spec->kind == SECURITY_OBJECT_FILE &&
        spec->scope == SECURITY_SCOPE_PROJECT && (spec->ceiling & ceiling & SECURITY_REPLACE))
        return SECURITY_APPROVAL_REQUIRED;
    if (!security_scope_permits(spec, operation)) return SECURITY_DENIED;
    if ((security_defaults(spec) | grants) & operation) return SECURITY_ALLOWED;
    if (spec->scope == SECURITY_SCOPE_PROJECT &&
            (operation == SECURITY_REPLACE || operation == SECURITY_APPEND)) return SECURITY_APPROVAL_REQUIRED;
    return SECURITY_DENIED;
}

SecurityDecision security_policy_scope_check(SecurityPolicy* policy,
        const SecurityResourceSpec* spec, SecurityOperations selected_operations, SecurityOperations operation) {
    if (!policy || !security_resource_spec_valid(spec) || !operation || (operation & (operation - 1)) ||
            !security_operations_valid(operation) || !security_operations_valid(selected_operations))
        return SECURITY_INVALID_REQUEST;
    pthread_mutex_lock(&policy->mutex);
    SecurityDecision result = security_scope_decision(spec, policy->config.ceiling,
        spec->scope == SECURITY_SCOPE_SELECTED ? selected_operations : 0, operation);
    pthread_mutex_unlock(&policy->mutex);
    return result;
}

SecurityPolicy* security_policy_create(const SecurityPolicyConfig* config) {
    if (!config || !config->principal || !config->session || !config->max_resources ||
        !config->max_grants || config->max_resources > INT_MAX || config->max_grants > INT_MAX ||
        !security_operations_valid(config->ceiling)) return nullptr;
    SecurityPolicy* policy = (SecurityPolicy*)mem_calloc(1, sizeof(SecurityPolicy), MEM_CAT_SYSTEM);
    if (!policy) return nullptr;
    policy->config = *config;
    policy->generation = 1;
    policy->next_resource = 1;
    policy->next_grant = 1;
    policy->resources = arraylist_new(0);
    policy->grants = arraylist_new(0);
    if (!policy->resources || !policy->grants || pthread_mutex_init(&policy->mutex, nullptr) != 0) {
        if (policy->resources) arraylist_free(policy->resources);
        if (policy->grants) arraylist_free(policy->grants);
        mem_free(policy);
        return nullptr;
    }
    return policy;
}

void security_policy_destroy(SecurityPolicy* policy) {
    if (!policy) return;
    // the host must join users before releasing the policy, as for its resource table
    for (int i = 0; i < policy->resources->length; i++) mem_free(policy->resources->data[i]);
    for (int i = 0; i < policy->grants->length; i++) mem_free(policy->grants->data[i]);
    arraylist_free(policy->resources);
    arraylist_free(policy->grants);
    pthread_mutex_destroy(&policy->mutex);
    mem_free(policy);
}

uint64_t security_policy_generation(SecurityPolicy* policy) {
    if (!policy) return 0;
    pthread_mutex_lock(&policy->mutex);
    uint64_t generation = policy->generation;
    pthread_mutex_unlock(&policy->mutex);
    return generation;
}

SecurityDecision security_policy_admit_resource(SecurityPolicy* policy,
        const SecurityResourceSpec* spec, SecurityResourceRef* out_resource) {
    if (out_resource) *out_resource = {};
    if (!policy || !out_resource || !security_resource_spec_valid(spec))
        return SECURITY_INVALID_REQUEST;
    pthread_mutex_lock(&policy->mutex);
    SecurityDecision result = SECURITY_LIMIT_EXCEEDED;
    SecurityResource* reusable = nullptr;
    for (int i = 0; i < policy->resources->length; i++) {
        SecurityResource* resource = (SecurityResource*)policy->resources->data[i];
        if (resource->revoked) { reusable = resource; break; }
    }
    if (policy->next_resource != UINT64_MAX &&
        (reusable || (uint64_t)policy->resources->length < policy->config.max_resources)) {
        SecurityResource* resource = reusable ? reusable
            : (SecurityResource*)mem_calloc(1, sizeof(SecurityResource), MEM_CAT_SYSTEM);
        if (resource) {
            if (reusable || arraylist_append(policy->resources, resource)) {
                *resource = {*spec, policy->next_resource++, false};
                out_resource->session = policy->config.session;
                out_resource->serial = resource->serial;
                result = SECURITY_ALLOWED;
            } else mem_free(resource);
        }
    }
    pthread_mutex_unlock(&policy->mutex);
    return result;
}

SecurityDecision security_policy_check(SecurityPolicy* policy, const SecurityRequest* request) {
    if (!policy || !request || !request->operation ||
        (request->operation & (request->operation - 1)) ||
        !security_operations_valid(request->operation)) return SECURITY_INVALID_REQUEST;
    pthread_mutex_lock(&policy->mutex);
    SecurityResource* resource = security_resource_locked(policy, request->resource);
    SecurityDecision result = SECURITY_DENIED;
    if (!resource) result = security_missing_resource_locked(policy, request->resource);
    else if (resource->revoked || request->generation != policy->generation) result = SECURITY_REVOKED;
    else {
        SecurityOperations grants = 0;
        for (int i = 0; i < policy->grants->length; i++) {
            SecurityGrant* grant = (SecurityGrant*)policy->grants->data[i];
            if (!grant->revoked && grant->resource.serial == request->resource.serial &&
                    (!grant->expires_at || request->now < grant->expires_at)) grants |= grant->operations;
        }
        result = security_scope_decision(&resource->spec, policy->config.ceiling, grants, request->operation);
    }
    pthread_mutex_unlock(&policy->mutex);
    return result;
}

SecurityDecision security_policy_grant(SecurityPolicy* policy, SecurityResourceRef ref,
        SecurityOperations operations, uint64_t expires_at, uint64_t* out_grant) {
    if (out_grant) *out_grant = 0;
    if (!policy || !out_grant || !operations || !security_operations_valid(operations))
        return SECURITY_INVALID_REQUEST;
    pthread_mutex_lock(&policy->mutex);
    SecurityResource* resource = security_resource_locked(policy, ref);
    SecurityDecision result = SECURITY_DENIED;
    if (!resource) result = security_missing_resource_locked(policy, ref);
    else if (resource->revoked) result = SECURITY_REVOKED;
    else if (!(operations & ~policy->config.ceiling) &&
        security_scope_permits(&resource->spec, operations)) {
        result = SECURITY_LIMIT_EXCEEDED;
        SecurityGrant* reusable = nullptr;
        for (int i = 0; i < policy->grants->length; i++) {
            SecurityGrant* grant = (SecurityGrant*)policy->grants->data[i];
            if (grant->revoked) { reusable = grant; break; }
        }
        if ((reusable || (uint64_t)policy->grants->length < policy->config.max_grants) &&
            policy->generation != UINT64_MAX && policy->next_grant != UINT64_MAX) {
            SecurityGrant* grant = reusable ? reusable
                : (SecurityGrant*)mem_calloc(1, sizeof(SecurityGrant), MEM_CAT_SYSTEM);
            if (grant) {
                if (reusable || arraylist_append(policy->grants, grant)) {
                    *grant = {ref, operations, expires_at, false, policy->next_grant++};
                    *out_grant = grant->serial;
                    policy->generation++;
                    result = SECURITY_ALLOWED;
                } else mem_free(grant);
            }
        }
    }
    pthread_mutex_unlock(&policy->mutex);
    return result;
}

bool security_policy_revoke_grant(SecurityPolicy* policy, uint64_t id) {
    if (!policy) return false;
    pthread_mutex_lock(&policy->mutex);
    bool valid = false;
    for (int i = 0; id && policy->generation != UINT64_MAX && i < policy->grants->length; i++) {
        SecurityGrant* grant = (SecurityGrant*)policy->grants->data[i];
        if (grant->serial == id && !grant->revoked) {
            grant->revoked = true; policy->generation++; valid = true; break;
        }
    }
    pthread_mutex_unlock(&policy->mutex);
    return valid;
}

static bool security_policy_close_resource(SecurityPolicy* policy, SecurityResourceRef ref, bool revoke) {
    if (!policy) return false;
    pthread_mutex_lock(&policy->mutex);
    SecurityResource* resource = security_resource_locked(policy, ref);
    bool valid = resource && !resource->revoked && (!revoke || policy->generation != UINT64_MAX);
    if (valid) {
        resource->revoked = true;
        for (int i = 0; i < policy->grants->length; i++) {
            SecurityGrant* grant = (SecurityGrant*)policy->grants->data[i];
            if (grant->resource.serial == ref.serial) grant->revoked = true;
        }
        if (revoke) policy->generation++;
    }
    pthread_mutex_unlock(&policy->mutex);
    return valid;
}

bool security_policy_revoke_resource(SecurityPolicy* policy, SecurityResourceRef ref) {
    return security_policy_close_resource(policy, ref, true);
}

bool security_policy_release_resource(SecurityPolicy* policy, SecurityResourceRef ref) {
    return security_policy_close_resource(policy, ref, false);
}

bool security_policy_restrict(SecurityPolicy* policy, SecurityOperations ceiling) {
    if (!policy || !security_operations_valid(ceiling)) return false;
    pthread_mutex_lock(&policy->mutex);
    bool valid = !(ceiling & ~policy->config.ceiling) && policy->generation != UINT64_MAX;
    if (valid && ceiling != policy->config.ceiling) {
        policy->config.ceiling = ceiling;
        policy->generation++;
    }
    pthread_mutex_unlock(&policy->mutex);
    return valid;
}

const char* security_decision_name(int32_t decision) {
    switch (decision) {
        case SECURITY_ALLOWED: return "allowed";
        case SECURITY_APPROVAL_REQUIRED: return "approval_required";
        case SECURITY_DENIED: return "denied";
        case SECURITY_REVOKED: return "revoked";
        case SECURITY_INVALID_REQUEST: return "invalid_request";
        case SECURITY_LIMIT_EXCEEDED: return "limit_exceeded";
    }
    return "invalid_request";
}
