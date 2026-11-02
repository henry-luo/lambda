#include <gtest/gtest.h>
#include <limits.h>
#include "../lambda/io/security_policy.h"

class SecurityPolicyTest : public ::testing::Test {
protected:
    SecurityPolicy* policy = nullptr;

    void SetUp() override {
        SecurityPolicyConfig config = {1, 101, SECURITY_ALL_OPERATIONS, 64, 64};
        policy = security_policy_create(&config);
        ASSERT_NE(policy, nullptr);
    }

    void TearDown() override { security_policy_destroy(policy); }

    SecurityResourceRef admit(SecurityScope scope, SecurityObjectKind kind,
            bool source = false, SecurityOperations ceiling = SECURITY_ALL_OPERATIONS) {
        SecurityResourceSpec spec = {scope, kind, ceiling, source};
        SecurityResourceRef ref = {};
        EXPECT_EQ(security_policy_admit_resource(policy, &spec, &ref), SECURITY_ALLOWED);
        return ref;
    }

    SecurityDecision check(SecurityResourceRef ref, SecurityOperations operation, uint64_t now = 0) {
        SecurityRequest request = {ref, security_policy_generation(policy), operation, now};
        return security_policy_check(policy, &request);
    }

    uint64_t grant(SecurityResourceRef ref, SecurityOperations operations, uint64_t expiry = 0) {
        uint64_t id = 0;
        EXPECT_EQ(security_policy_grant(policy, ref, operations, expiry, &id), SECURITY_ALLOWED);
        EXPECT_NE(id, 0U);
        return id;
    }
};

TEST_F(SecurityPolicyTest, ProjectDefaultsSeparateCreationFromMutation) {
    SecurityResourceRef file = admit(SECURITY_SCOPE_PROJECT, SECURITY_OBJECT_FILE, true);
    SecurityResourceRef dir = admit(SECURITY_SCOPE_PROJECT, SECURITY_OBJECT_DIRECTORY);
    SecurityResourceRef fresh = admit(SECURITY_SCOPE_PROJECT, SECURITY_OBJECT_NEW_FILE);
    SecurityResourceRef fresh_dir = admit(SECURITY_SCOPE_PROJECT, SECURITY_OBJECT_NEW_DIRECTORY);
    EXPECT_EQ(check(file, SECURITY_READ), SECURITY_ALLOWED);
    EXPECT_EQ(check(file, SECURITY_METADATA), SECURITY_ALLOWED);
    EXPECT_EQ(check(file, SECURITY_LOAD_SOURCE), SECURITY_ALLOWED);
    EXPECT_EQ(check(dir, SECURITY_ENUMERATE), SECURITY_ALLOWED);
    EXPECT_EQ(check(fresh, SECURITY_CREATE), SECURITY_ALLOWED);
    EXPECT_EQ(check(fresh_dir, SECURITY_CREATE), SECURITY_ALLOWED);
    EXPECT_EQ(check(file, SECURITY_REPLACE), SECURITY_APPROVAL_REQUIRED);
    EXPECT_EQ(check(file, SECURITY_APPEND), SECURITY_APPROVAL_REQUIRED);
    EXPECT_EQ(check(file, SECURITY_CREATE), SECURITY_APPROVAL_REQUIRED);
    EXPECT_EQ(check(file, SECURITY_DELETE), SECURITY_DENIED);
    EXPECT_EQ(check(file, SECURITY_RENAME), SECURITY_DENIED);
    EXPECT_EQ(check(file, SECURITY_WATCH), SECURITY_DENIED);
    EXPECT_EQ(check(dir, SECURITY_CREATE), SECURITY_DENIED);
    EXPECT_EQ(check(fresh, SECURITY_REPLACE), SECURITY_DENIED);
}

TEST_F(SecurityPolicyTest, ReadingDataDoesNotAdmitSourceCode) {
    SecurityResourceRef data = admit(SECURITY_SCOPE_PROJECT, SECURITY_OBJECT_FILE);
    EXPECT_EQ(check(data, SECURITY_READ), SECURITY_ALLOWED);
    EXPECT_EQ(check(data, SECURITY_LOAD_SOURCE), SECURITY_DENIED);
    uint64_t id = 99;
    EXPECT_EQ(security_policy_grant(policy, data, SECURITY_LOAD_SOURCE, 0, &id), SECURITY_DENIED);
    EXPECT_EQ(id, 0U);
}

TEST_F(SecurityPolicyTest, LooseEntryAndShippedLibrariesDoNotGrantSiblingAccess) {
    SecurityResourceRef entry = admit(SECURITY_SCOPE_ENTRY, SECURITY_OBJECT_FILE, true);
    SecurityResourceRef shipped = admit(SECURITY_SCOPE_SHIPPED, SECURITY_OBJECT_FILE, true);
    SecurityResourceRef sibling = admit(SECURITY_SCOPE_SELECTED, SECURITY_OBJECT_FILE, true);
    EXPECT_EQ(check(entry, SECURITY_LOAD_SOURCE), SECURITY_ALLOWED);
    EXPECT_EQ(check(shipped, SECURITY_LOAD_SOURCE), SECURITY_ALLOWED);
    EXPECT_EQ(check(sibling, SECURITY_READ), SECURITY_DENIED);
    EXPECT_EQ(check(sibling, SECURITY_LOAD_SOURCE), SECURITY_DENIED);
    EXPECT_EQ(check(shipped, SECURITY_REPLACE), SECURITY_DENIED);
    uint64_t id = 0;
    EXPECT_EQ(security_policy_grant(policy, shipped, SECURITY_REPLACE, 0, &id), SECURITY_DENIED);
}

TEST_F(SecurityPolicyTest, SelectedResourceGrantCoversOnlyItsObjectAndOperations) {
    SecurityResourceRef chosen = admit(SECURITY_SCOPE_SELECTED, SECURITY_OBJECT_FILE);
    SecurityResourceRef other = admit(SECURITY_SCOPE_SELECTED, SECURITY_OBJECT_FILE);
    EXPECT_EQ(check(chosen, SECURITY_READ), SECURITY_DENIED);
    grant(chosen, SECURITY_READ);
    EXPECT_EQ(check(chosen, SECURITY_READ), SECURITY_ALLOWED);
    EXPECT_EQ(check(chosen, SECURITY_APPEND), SECURITY_DENIED);
    EXPECT_EQ(check(chosen, SECURITY_ENUMERATE), SECURITY_DENIED);
    EXPECT_EQ(check(other, SECURITY_READ), SECURITY_DENIED);
}

TEST_F(SecurityPolicyTest, ApprovedReplacementDoesNotAuthorizeCreateOnlyTruncation) {
    SecurityResourceRef ref = admit(SECURITY_SCOPE_PROJECT, SECURITY_OBJECT_FILE);
    grant(ref, SECURITY_REPLACE);
    EXPECT_EQ(check(ref, SECURITY_REPLACE), SECURITY_ALLOWED);
    EXPECT_EQ(check(ref, SECURITY_APPEND), SECURITY_APPROVAL_REQUIRED);
    EXPECT_EQ(check(ref, SECURITY_CREATE), SECURITY_APPROVAL_REQUIRED);
}

TEST_F(SecurityPolicyTest, ProtectedResourcesCannotBeGranted) {
    const SecurityObjectKind kinds[] = {SECURITY_OBJECT_FILE, SECURITY_OBJECT_DIRECTORY,
        SECURITY_OBJECT_NEW_FILE, SECURITY_OBJECT_NEW_DIRECTORY};
    for (SecurityObjectKind kind : kinds) {
        SecurityResourceRef ref = admit(SECURITY_SCOPE_PROTECTED, kind);
        for (SecurityOperations operation = 1; operation <= SECURITY_INVOKE; operation <<= 1) {
            EXPECT_EQ(check(ref, operation), SECURITY_DENIED);
            uint64_t id = 7;
            EXPECT_EQ(security_policy_grant(policy, ref, operation, 0, &id), SECURITY_DENIED);
            EXPECT_EQ(id, 0U);
        }
    }
}

TEST_F(SecurityPolicyTest, PrivateStorageDoesNotGrantProjectMutation) {
    SecurityResourceRef private_file = admit(SECURITY_SCOPE_PRIVATE, SECURITY_OBJECT_FILE);
    SecurityResourceRef root_file = admit(SECURITY_SCOPE_PROJECT, SECURITY_OBJECT_FILE);
    EXPECT_EQ(check(private_file, SECURITY_APPEND), SECURITY_ALLOWED);
    EXPECT_EQ(check(private_file, SECURITY_REPLACE), SECURITY_ALLOWED);
    EXPECT_EQ(check(private_file, SECURITY_DELETE), SECURITY_ALLOWED);
    EXPECT_EQ(check(private_file, SECURITY_LOAD_SOURCE), SECURITY_DENIED);
    EXPECT_EQ(check(root_file, SECURITY_REPLACE), SECURITY_APPROVAL_REQUIRED);
}

TEST_F(SecurityPolicyTest, NetworkAndServicesHaveNoAmbientAuthority) {
    const SecurityScope scopes[] = {SECURITY_SCOPE_PROJECT, SECURITY_SCOPE_PRIVATE, SECURITY_SCOPE_SELECTED};
    for (SecurityScope scope : scopes) {
        SecurityResourceRef endpoint = admit(scope, SECURITY_OBJECT_ENDPOINT);
        SecurityResourceRef service = admit(scope, SECURITY_OBJECT_SERVICE);
        EXPECT_EQ(check(endpoint, SECURITY_CONNECT), SECURITY_DENIED);
        EXPECT_EQ(check(endpoint, SECURITY_LISTEN), SECURITY_DENIED);
        EXPECT_EQ(check(service, SECURITY_INVOKE), SECURITY_DENIED);
    }
    SecurityResourceRef endpoint = admit(SECURITY_SCOPE_SELECTED, SECURITY_OBJECT_ENDPOINT);
    grant(endpoint, SECURITY_CONNECT);
    EXPECT_EQ(check(endpoint, SECURITY_CONNECT), SECURITY_ALLOWED);
    EXPECT_EQ(check(endpoint, SECURITY_LISTEN), SECURITY_DENIED);
}

TEST_F(SecurityPolicyTest, ObjectAndProfileCeilingsCannotBeOverridden) {
    SecurityResourceRef ref = admit(SECURITY_SCOPE_PROJECT, SECURITY_OBJECT_FILE,
        false, SECURITY_READ);
    EXPECT_EQ(check(ref, SECURITY_READ), SECURITY_ALLOWED);
    EXPECT_EQ(check(ref, SECURITY_REPLACE), SECURITY_DENIED);
    EXPECT_EQ(check(ref, SECURITY_CREATE), SECURITY_DENIED);
    uint64_t id = 0;
    EXPECT_EQ(security_policy_grant(policy, ref, SECURITY_REPLACE, 0, &id), SECURITY_DENIED);
    EXPECT_TRUE(security_policy_restrict(policy, SECURITY_METADATA));
    EXPECT_EQ(check(ref, SECURITY_READ), SECURITY_DENIED);
    EXPECT_EQ(security_policy_grant(policy, ref, SECURITY_READ, 0, &id), SECURITY_DENIED);
    EXPECT_FALSE(security_policy_restrict(policy, SECURITY_ALL_OPERATIONS));
}

TEST_F(SecurityPolicyTest, ExpiringGrantsDoNotExtendEachOther) {
    SecurityResourceRef ref = admit(SECURITY_SCOPE_SELECTED, SECURITY_OBJECT_FILE);
    grant(ref, SECURITY_READ, 10);
    grant(ref, SECURITY_APPEND, 20);
    EXPECT_EQ(check(ref, SECURITY_READ, 9), SECURITY_ALLOWED);
    EXPECT_EQ(check(ref, SECURITY_READ, 10), SECURITY_DENIED);
    EXPECT_EQ(check(ref, SECURITY_APPEND, 19), SECURITY_ALLOWED);
    EXPECT_EQ(check(ref, SECURITY_APPEND, 20), SECURITY_DENIED);
}

TEST_F(SecurityPolicyTest, RevocationInvalidatesDispatchAndSensitiveCompletion) {
    SecurityResourceRef ref = admit(SECURITY_SCOPE_SELECTED, SECURITY_OBJECT_FILE);
    uint64_t id = grant(ref, SECURITY_READ);
    SecurityRequest queued = {ref, security_policy_generation(policy), SECURITY_READ, 0};
    ASSERT_EQ(security_policy_check(policy, &queued), SECURITY_ALLOWED);
    EXPECT_TRUE(security_policy_revoke_grant(policy, id));
    EXPECT_EQ(security_policy_check(policy, &queued), SECURITY_REVOKED);
    EXPECT_EQ(check(ref, SECURITY_READ), SECURITY_DENIED);
    EXPECT_FALSE(security_policy_revoke_grant(policy, id));
    grant(ref, SECURITY_READ);
    EXPECT_EQ(security_policy_check(policy, &queued), SECURITY_REVOKED);
    EXPECT_EQ(check(ref, SECURITY_READ), SECURITY_ALLOWED);
}

TEST_F(SecurityPolicyTest, RevokedResourcesStayRevokedAfterNewAdmissions) {
    SecurityResourceRef ref = admit(SECURITY_SCOPE_PROJECT, SECURITY_OBJECT_FILE);
    ASSERT_TRUE(security_policy_revoke_resource(policy, ref));
    SecurityResourceRef next = admit(SECURITY_SCOPE_PROJECT, SECURITY_OBJECT_FILE);
    EXPECT_NE(ref.serial, next.serial);
    EXPECT_EQ(check(ref, SECURITY_READ), SECURITY_REVOKED);
    EXPECT_EQ(check(next, SECURITY_READ), SECURITY_ALLOWED);
    uint64_t id = 0;
    EXPECT_EQ(security_policy_grant(policy, ref, SECURITY_READ, 0, &id), SECURITY_REVOKED);
}

TEST_F(SecurityPolicyTest, CrossSessionAndForgedResourcesFailClosed) {
    SecurityResourceRef ref = admit(SECURITY_SCOPE_PROJECT, SECURITY_OBJECT_FILE);
    SecurityPolicyConfig config = {2, 202, SECURITY_ALL_OPERATIONS, 8, 8};
    SecurityPolicy* other = security_policy_create(&config);
    ASSERT_NE(other, nullptr);
    SecurityRequest request = {ref, security_policy_generation(other), SECURITY_READ, 0};
    EXPECT_EQ(security_policy_check(other, &request), SECURITY_INVALID_REQUEST);
    security_policy_destroy(other);
    ref.serial = UINT64_MAX;
    EXPECT_EQ(check(ref, SECURITY_READ), SECURITY_INVALID_REQUEST);
    ref.serial = 0;
    EXPECT_EQ(check(ref, SECURITY_READ), SECURITY_INVALID_REQUEST);
}

TEST_F(SecurityPolicyTest, ResourceAndGrantBudgetsFailWithoutPublishingAuthority) {
    SecurityResourceRef ref = {};
    for (int i = 0; i < 64; i++) ref = admit(SECURITY_SCOPE_SELECTED, SECURITY_OBJECT_FILE);
    SecurityResourceSpec spec = {SECURITY_SCOPE_PROJECT, SECURITY_OBJECT_NEW_FILE,
        SECURITY_ALL_OPERATIONS, false};
    SecurityResourceRef extra = {99, 99};
    EXPECT_EQ(security_policy_admit_resource(policy, &spec, &extra), SECURITY_LIMIT_EXCEEDED);
    EXPECT_EQ(extra.serial, 0U);
    EXPECT_EQ(extra.session, 0U);
    for (int i = 0; i < 64; i++) grant(ref, SECURITY_READ);
    uint64_t generation = security_policy_generation(policy);
    uint64_t id = 99;
    EXPECT_EQ(security_policy_grant(policy, ref, SECURITY_APPEND, 0, &id), SECURITY_LIMIT_EXCEEDED);
    EXPECT_EQ(id, 0U);
    EXPECT_EQ(security_policy_generation(policy), generation);
    EXPECT_EQ(check(ref, SECURITY_APPEND), SECURITY_DENIED);
}

TEST_F(SecurityPolicyTest, InvalidRequestsAndUnknownBitsFailClosed) {
    SecurityResourceRef ref = admit(SECURITY_SCOPE_PROJECT, SECURITY_OBJECT_FILE);
    EXPECT_EQ(check(ref, 0), SECURITY_INVALID_REQUEST);
    EXPECT_EQ(check(ref, SECURITY_READ | SECURITY_REPLACE), SECURITY_INVALID_REQUEST);
    EXPECT_EQ(check(ref, 1ULL << 63), SECURITY_INVALID_REQUEST);
    EXPECT_EQ(security_policy_check(nullptr, nullptr), SECURITY_INVALID_REQUEST);
    EXPECT_EQ(security_policy_check(policy, nullptr), SECURITY_INVALID_REQUEST);
    EXPECT_EQ(security_policy_generation(nullptr), 0U);
    EXPECT_FALSE(security_policy_revoke_grant(nullptr, 1));
    EXPECT_FALSE(security_policy_revoke_resource(nullptr, ref));
    EXPECT_FALSE(security_policy_restrict(nullptr, 0));
    SecurityResourceSpec invalid = {-1, SECURITY_OBJECT_FILE,
        SECURITY_ALL_OPERATIONS, false};
    SecurityResourceRef out = {};
    EXPECT_EQ(security_policy_admit_resource(policy, &invalid, &out), SECURITY_INVALID_REQUEST);
    invalid = {SECURITY_SCOPE_PROJECT, SECURITY_OBJECT_ENDPOINT, SECURITY_ALL_OPERATIONS, true};
    EXPECT_EQ(security_policy_admit_resource(policy, &invalid, &out), SECURITY_INVALID_REQUEST);
    invalid = {SECURITY_SCOPE_PROJECT, 99, SECURITY_ALL_OPERATIONS, false};
    EXPECT_EQ(security_policy_admit_resource(policy, &invalid, &out), SECURITY_INVALID_REQUEST);
}

TEST(SecurityPolicyConfiguration, MissingIdentityOrUnknownCeilingDoesNotBecomeTrusted) {
    EXPECT_EQ(security_policy_create(nullptr), nullptr);
    SecurityPolicyConfig config = {1, 0, SECURITY_ALL_OPERATIONS, 8, 8};
    EXPECT_EQ(security_policy_create(&config), nullptr);
    config = {0, 1, SECURITY_ALL_OPERATIONS, 8, 8};
    EXPECT_EQ(security_policy_create(&config), nullptr);
    config = {1, 1, 1ULL << 63, 8, 8};
    EXPECT_EQ(security_policy_create(&config), nullptr);
    config = {1, 1, SECURITY_ALL_OPERATIONS, UINT32_MAX, 8};
    EXPECT_EQ(security_policy_create(&config), nullptr);
    EXPECT_STREQ(security_decision_name(SECURITY_APPROVAL_REQUIRED), "approval_required");
    EXPECT_STREQ(security_decision_name(99), "invalid_request");
}

TEST_F(SecurityPolicyTest, ClosedLeasesReuseCapacityWithoutReusingAuthority) {
    SecurityResourceRef old = {};
    uint64_t old_grant = 0;
    for (int i = 0; i < 512; i++) {
        SecurityResourceRef current = admit(SECURITY_SCOPE_SELECTED, SECURITY_OBJECT_FILE);
        uint64_t id = grant(current, SECURITY_READ);
        EXPECT_GT(current.serial, old.serial);
        EXPECT_GT(id, old_grant);
        if (old.serial) EXPECT_EQ(check(old, SECURITY_READ), SECURITY_REVOKED);
        if (old_grant) EXPECT_FALSE(security_policy_revoke_grant(policy, old_grant));
        EXPECT_EQ(check(current, SECURITY_READ), SECURITY_ALLOWED);
        uint64_t generation = security_policy_generation(policy);
        ASSERT_TRUE(security_policy_release_resource(policy, current));
        EXPECT_EQ(security_policy_generation(policy), generation);
        EXPECT_EQ(check(current, SECURITY_READ), SECURITY_REVOKED);
        old = current; old_grant = id;
    }
}

TEST_F(SecurityPolicyTest, NamespacePreflightConsumesNoAuthorityOrSerials) {
    SecurityResourceSpec spec = {SECURITY_SCOPE_SELECTED, SECURITY_OBJECT_FILE,
        SECURITY_READ | SECURITY_METADATA, false};
    uint64_t generation = security_policy_generation(policy);
    for (int i = 0; i < 1000; i++) {
        EXPECT_EQ(security_policy_scope_check(policy, &spec, SECURITY_READ, SECURITY_READ), SECURITY_ALLOWED);
        EXPECT_EQ(security_policy_scope_check(policy, &spec, SECURITY_READ, SECURITY_METADATA), SECURITY_DENIED);
    }
    EXPECT_EQ(security_policy_generation(policy), generation);
    SecurityResourceRef resource = {};
    ASSERT_EQ(security_policy_admit_resource(policy, &spec, &resource), SECURITY_ALLOWED);
    EXPECT_EQ(resource.serial, 1U);
    EXPECT_EQ(check(resource, SECURITY_READ), SECURITY_DENIED);
    spec.scope = SECURITY_SCOPE_PROJECT;
    spec.ceiling = SECURITY_ALL_OPERATIONS;
    EXPECT_EQ(security_policy_scope_check(policy, &spec, 0, SECURITY_CREATE), SECURITY_APPROVAL_REQUIRED);
    spec.kind = SECURITY_OBJECT_NEW_FILE;
    EXPECT_EQ(security_policy_scope_check(policy, &spec, 0, SECURITY_CREATE), SECURITY_ALLOWED);
    spec.kind = -1;
    EXPECT_EQ(security_policy_scope_check(policy, &spec, 0, SECURITY_READ), SECURITY_INVALID_REQUEST);
}

TEST_F(SecurityPolicyTest, ClosingAnotherLeaseDoesNotInvalidatePendingWork) {
    SecurityResourceRef active = admit(SECURITY_SCOPE_PROJECT, SECURITY_OBJECT_FILE);
    SecurityRequest pending = {active, security_policy_generation(policy), SECURITY_READ, 0};
    SecurityResourceRef temporary = admit(SECURITY_SCOPE_PROJECT, SECURITY_OBJECT_FILE);
    EXPECT_TRUE(security_policy_release_resource(policy, temporary));
    EXPECT_EQ(security_policy_check(policy, &pending), SECURITY_ALLOWED);
    EXPECT_FALSE(security_policy_release_resource(policy, temporary));
}
