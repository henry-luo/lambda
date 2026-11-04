#include <gtest/gtest.h>
#include "../lambda/host/safety/update_download.h"
#include "../lambda/host/safety/update_refresh.h"
#include "../lambda/host/safety/update_metadata.h"
#include "../lambda/host/safety/update_stage.h"
#include "../lib/digest.h"
#include "../lib/curl_runtime.h"
#include "../lib/log.h"
#include "../lib/mem.h"
#include "../lib/file_utils.h"
#include "../lib/hex.h"
#include "../lib/trust_store.h"
#include "../lib/https_url.h"
#include "safety/update_fixture.h"
#include <stdlib.h>
#ifndef _WIN32
#include <arpa/inet.h>
#include <unistd.h>

TEST(SecurityUpdatePull, ActualNumericPeerMustBePublicUnicast) {
    const char* blocked4[] = {"0.0.0.0", "10.0.0.1", "100.64.0.1", "100.127.255.255", "127.0.0.1",
        "169.254.169.254", "172.16.0.1", "172.31.255.255", "192.0.0.9", "192.0.2.1", "192.88.99.1",
        "192.168.0.1", "198.18.0.1", "198.19.255.255", "198.51.100.1", "203.0.113.1", "224.0.0.1", "255.255.255.255"};
    const char* allowed4[] = {"1.1.1.1", "8.8.8.8", "100.63.255.255", "100.128.0.0", "172.15.255.255", "172.32.0.0", "198.20.0.0"};
    const char* blocked6[] = {"::", "::1", "::ffff:8.8.8.8", "64:ff9b::808:808", "64:ff9b:1::1", "100::1",
        "2001::1", "2001:1ff:ffff::1", "2001:db8::1", "2002:808:808::1", "3fff::1", "3fff:f:ffff::1", "fc00::1", "fe80::1", "ff02::1"};
    const char* allowed6[] = {"2001:4860:4860::8888", "2606:4700:4700::1111", "2001:200::1", "3fff:1000::1"};
    uint8_t bytes[16];
    for (const char* address : blocked4) { ASSERT_EQ(inet_pton(AF_INET, address, bytes), 1); EXPECT_FALSE(https_pull_public_address(4, bytes, 4)) << address; }
    for (const char* address : allowed4) { ASSERT_EQ(inet_pton(AF_INET, address, bytes), 1); EXPECT_TRUE(https_pull_public_address(4, bytes, 4)) << address; }
    for (const char* address : blocked6) { ASSERT_EQ(inet_pton(AF_INET6, address, bytes), 1); EXPECT_FALSE(https_pull_public_address(6, bytes, 16)) << address; }
    for (const char* address : allowed6) { ASSERT_EQ(inet_pton(AF_INET6, address, bytes), 1); EXPECT_TRUE(https_pull_public_address(6, bytes, 16)) << address; }
    EXPECT_FALSE(https_pull_public_address(4, nullptr, 4)); EXPECT_FALSE(https_pull_public_address(4, bytes, 16));
    EXPECT_FALSE(https_pull_public_address(6, bytes, 4)); EXPECT_FALSE(https_pull_public_address(5, bytes, 16));
}
TEST(SecurityUpdatePull, EveryHopRequiresTheSameCanonicalAdmittedOriginProfile) {
    SecurityUpdatePackageSpec spec = update_fixture_spec("lambda.fixture");
    EXPECT_TRUE(security_update_url_allowed(&spec, "https://updates.example.org/repository/timestamp.json"));
    EXPECT_TRUE(security_update_url_allowed(&spec, "https://cdn.example.org/targets/sha256.file"));
    EXPECT_TRUE(security_update_url_allowed(&spec, "https://updates.example.org/repository/1...%2F%E5%90%8D%23%3Frole.json"));
    const char* denied[] = {nullptr, "http://updates.example.org/timestamp.json", "https://other.example.org/a",
        "https://updates.example.org:444/a", "https://UPDATES.example.org/a", "https://updates.example.org:443/a",
        "https://updates.example.org.evil.org/a", "https://updates.example.org@other.example.org/a",
        "https://updates.example.org/a?user=secret", "https://updates.example.org/a#secret", "https://updates.example.org/%2e/a",
        "https://updates.example.org/a/../b", "https://updates.example.org/a/..", "https://updates.example.org/.",
        "https://updates.example.org/./b", "https://updates.example.org\\a", "https://127.0.0.1/a",
        "https://[::1]/a", "https://local/a", "https://updates.local/a", "https://updates.example.org/a\r\nCookie:secret"};
    for (const char* address : denied) EXPECT_FALSE(security_update_url_allowed(&spec, address)) << (address ? address : "null");
    spec.root[0] = 0; memset(spec.root, 0, sizeof(spec.root));
    EXPECT_FALSE(security_update_url_allowed(&spec, "https://updates.example.org/a"));
}
TEST(SecurityUpdatePull, EncodedPathDataCannotChangeOriginOrInjectUrlControls) {
    const char* valid[] = {"file%20name.json", "a%2Fb.json", "..%2Fopaque.json", "a%252Fb.json", "%E5%90%8D.json", "%25literal.json", "a%23b%3Fc%40d.json", "a~b.json"};
    const char* denied[] = {"../escape", "a/../b", "a//b", "/root", "a?query=x", "a#fragment", "a\\b", "%2E/a", "%41", "%2f", "%00", "%0A", "%7F", "%C2%80", "%C0%AF", "%ED%A0%80", "%F4%90%80%80", "%FF", "%", "%2", "%GG"};
    for (const char* path : valid) EXPECT_TRUE(https_url_public_path_valid(path, 4096)) << path;
    for (const char* path : denied) EXPECT_FALSE(https_url_public_path_valid(path, 4096)) << path;
    EXPECT_FALSE(https_url_reference_valid_ex("https://updates.example.org%2Fevil/a", 4096, true));
    EXPECT_FALSE(https_url_reference_valid_ex("//updates.example.org%40evil/a", 4096, true));
    EXPECT_TRUE(https_url_target_name_valid("docs/名 file%20name.pkg", 4096));
    EXPECT_FALSE(https_url_target_name_valid("docs/../private", 4096));
}
static bool pull_already_revoked(void* context) { ++*(unsigned*)context; return false; }
TEST(SecurityUpdatePull, InvalidOrRevokedRequestsPerformNoNetworkInitialization) {
    unsigned checks = 0;
    HttpsPullRequest request = {"https://updates.example.org/a", 1024, 1000, pull_already_revoked, &checks};
    HttpsPullResponse response;
    EXPECT_EQ(https_pull_once(&request, &response), HTTPS_PULL_CANCELLED); EXPECT_EQ(checks, 1U);
    EXPECT_EQ(response.bytes, nullptr); EXPECT_EQ(response.received_bytes, 0U);
    request.max_bytes = 0; EXPECT_EQ(https_pull_once(&request, &response), HTTPS_PULL_INVALID);
    request.max_bytes = 1024; request.timeout_ms = 600001; EXPECT_EQ(https_pull_once(&request, &response), HTTPS_PULL_INVALID);
    request.timeout_ms = 1000; request.url = "https://user:secret@updates.example.org/a";
    EXPECT_EQ(https_pull_once(&request, &response), HTTPS_PULL_INVALID); EXPECT_EQ(checks, 1U);
    SecurityUpdateDownload result;
    EXPECT_EQ(security_update_download(nullptr, "timestamp.json", &result), HTTPS_PULL_CANCELLED);
    EXPECT_EQ(result.policy, SECURITY_UPDATE_INVALID); EXPECT_EQ(result.hops, 0U);
    security_update_download_free(&result);
}

static uint64_t pull_clock(void*) { return (uint64_t)time(nullptr); }
// This mode exists only in the standalone fixture binary; no production host
// flag, environment override, runtime function or worker opcode reaches it.
static int pull_fixture_main(char** args) {
    const char* home = args[0]; unsigned port = (unsigned)strtoul(args[1], nullptr, 10);
    const char* path = args[2]; const char* ca = args[3]; const char* mode = args[4];
    uint64_t limit = strtoull(args[5], nullptr, 10); unsigned seconds = (unsigned)strtoul(args[6], nullptr, 10);
    SecurityStoreResult stored; SecurityUpdateResult policy = SECURITY_UPDATE_INVALID;
    SecurityGrantStore* store = security_grant_store_test_open(home, &stored);
    if (!store) return 2;
    policy = security_update_initialize(store);
    SecurityUpdateControl* control = (policy == SECURITY_UPDATE_OK || policy == SECURITY_UPDATE_CONFLICT)
        ? security_update_open(store, pull_clock, nullptr, &policy) : nullptr;
    SecurityUpdatePackageSpec spec = update_fixture_spec("lambda.fixture");
    if (port != 443) {
        snprintf(spec.metadata_base, sizeof(spec.metadata_base), "https://updates.example.org:%u/repository/", port);
        snprintf(spec.origins[0], sizeof(spec.origins[0]), "https://updates.example.org:%u", port);
        snprintf(spec.origins[1], sizeof(spec.origins[1]), "https://cdn.example.org:%u", port);
    }
    spec.job_seconds = seconds; spec.max_download_bytes = limit;
    if (control) policy = security_update_admit(control, &spec, false);
    SecurityUpdateJob* job = policy == SECURITY_UPDATE_OK ? security_update_begin(control, spec.package, true, &policy) : nullptr;
    SecurityUpdateDownload result = {}; result.policy = policy; result.response.status = HTTPS_PULL_INVALID;
    char resolve[128]; snprintf(resolve, sizeof(resolve), "*:%u:127.0.0.1", port);
    bool limited = !strcmp(mode, "fixture-zero") || !strcmp(mode, "fixture-exact");
    HttpsPullFixture fixture = {resolve, ca, !strcmp(mode, "fixture") || limited};
    if (job) {
        if (limited) security_update_download_limited_fixture(job, path, limit,
            !strcmp(mode, "fixture-zero") ? 0 : 32, &fixture, &result);
        else security_update_download_fixture(job, path, &fixture, &result);
    }
    uint8_t digest[32] = {}; char hash[65];
    digest_sha256(result.response.bytes, result.response.size, digest);
    for (size_t i = 0; i < 32; i++) snprintf(hash + i * 2, 3, "%02x", digest[i]);
    char output[512]; int count = snprintf(output, sizeof(output),
        "{\"status\":%u,\"policy\":\"%s\",\"http\":%u,\"bytes\":%zu,\"received\":%llu,\"hops\":%u,\"transport\":%d,\"sha256\":\"%s\"}\n",
        (unsigned)result.response.status, security_update_result_name(result.policy), result.response.http_status,
        result.response.size, (unsigned long long)result.response.received_bytes, result.hops, result.response.transport_error, hash);
    bool wrote = count > 0 && (size_t)count < sizeof(output) && write(STDOUT_FILENO, output, (size_t)count) == count;
    security_update_download_free(&result); security_update_job_free(job); security_update_close(control); security_grant_store_close(store);
    return wrote ? 0 : 2;
}
#ifdef LAMBDA_SECURITY_UPDATE_TESTING
static int refresh_fixture_main(char** args, int stage_fd = -1, const char* root_path = "test/safety/tuf/fixtures/root.json") {
    int fd = atoi(args[0]); uint64_t session = strtoull(args[1], nullptr, 10);
    const char* home = args[2]; unsigned port = (unsigned)strtoul(args[3], nullptr, 10);
    const char* scenario = args[5]; const char* mode = args[7];
    SecurityStoreResult stored; SecurityUpdateResult policy;
    SecurityGrantStore* store = security_grant_store_test_open(home, &stored);
    if (!store) return 2;
    policy = security_update_initialize(store);
    SecurityUpdateControl* control = policy == SECURITY_UPDATE_OK || policy == SECURITY_UPDATE_CONFLICT ?
        security_update_open(store, pull_clock, nullptr, &policy) : nullptr;
    SecurityUpdatePackageSpec spec = update_fixture_spec("lambda.fixture");
    snprintf(spec.metadata_base, sizeof(spec.metadata_base), "https://updates.example.org:%u/tuf/%s/", port, scenario);
    snprintf(spec.origins[0], sizeof(spec.origins[0]), "https://updates.example.org:%u", port);
    spec.origin_count = 1; spec.job_seconds = 20; spec.max_download_bytes = strtoull(args[6], nullptr, 10);
    char* root = nullptr; size_t root_size = 0;
    if (!control || !file_read_all(root_path, MEM_CAT_SYSTEM, &root, &root_size) ||
        !digest_sha256(root, root_size, spec.root)) { security_update_close(control); security_grant_store_close(store); mem_free(root); return 2; }
    policy = security_update_admit(control, &spec, false);
    if (policy == SECURITY_UPDATE_OK) policy = security_update_metadata_initialize(control, spec.package, root, root_size);
    mem_free(root);
    if (policy != SECURITY_UPDATE_OK && policy != SECURITY_UPDATE_CONFLICT) { security_update_close(control); security_grant_store_close(store); return 2; }
    SecurityUpdateJob* job = security_update_begin(control, spec.package, true, &policy);
    char resolve[128]; snprintf(resolve, sizeof(resolve), "*:%u:127.0.0.1", port);
    HttpsPullFixture fixture = {resolve, args[4], true}; SecurityUpdateRefreshReport result = {};
    bool success = job && security_update_refresh_fixture(fd, session, job, "lambda.pkg", &fixture, &result);
    SecurityVerifiedUpdateView view = {}; bool visible = security_update_verified_view(result.artifact, job, &view);
    uint8_t digest[32] = {}; char hash[65];
    if (visible) digest_sha256(view.bytes, view.size, digest);
    hex_encode(digest, sizeof(digest), hash);
    uint8_t description_digest[32] = {}; char description_hash[65];
    if (visible) digest_sha256(view.description, view.description_size, description_digest);
    hex_encode(description_digest, sizeof(description_digest), description_hash);
    bool description = visible && view.description_size && memchr(view.description, 0, view.description_size) == nullptr;
    bool after = visible;
    if (visible && !strcmp(mode, "pause-after")) {
        security_update_set_mode(control, nullptr, SECURITY_UPDATE_PAUSED, 0);
        after = security_update_verified_view(result.artifact, job, &view);
    }
    if (visible && !strcmp(mode, "new-job")) {
        security_update_job_finish(job, true); security_update_job_free(job);
        job = security_update_begin(control, spec.package, true, &policy);
        after = security_update_verified_view(result.artifact, job, &view);
    }
    SecurityUpdateStageReport stage = {};
    bool staged = stage_fd >= 0 && security_update_stage_fixture(stage_fd, session, job, result.artifact, &stage);
    char output[768]; int count = snprintf(output, sizeof(output),
        "{\"ok\":%s,\"view\":%s,\"after\":%s,\"description\":%s,\"requests\":%u,\"checkpoints\":%u,"
        "\"received\":%llu,\"generation\":%llu,\"policy\":\"%s\",\"transport\":%u,\"sha256\":\"%s\",\"description_sha256\":\"%s\"}\n",
        success ? "true" : "false", visible ? "true" : "false", after ? "true" : "false", description ? "true" : "false",
        result.requests, result.checkpoints, (unsigned long long)result.received_bytes, (unsigned long long)result.generation,
        security_update_result_name(result.policy), (unsigned)result.transport, hash, description_hash);
    if (stage_fd >= 0 && count > 1 && (size_t)count < sizeof(output)) {
        char id[33]; hex_encode(stage.info.id, 16, id);
        count -= 2;
        int extra = snprintf(output + count, sizeof(output) - count,
            ",\"staged\":%s,\"stage_ready\":%s,\"stage_id\":\"%s\",\"stage_entries\":%u,\"stage_bytes\":%llu}\n",
            staged ? "true" : "false", stage.info.ready ? "true" : "false", id, stage.info.entries, (unsigned long long)stage.info.bytes);
        if (extra < 0 || (size_t)extra >= sizeof(output) - count) count = -1; else count += extra;
    }
    bool wrote = count > 0 && (size_t)count < sizeof(output) && write(1, output, (size_t)count) == count;
    security_update_stage_free(&stage); security_update_refresh_free(&result); security_update_job_free(job); security_update_close(control); security_grant_store_close(store);
    return wrote ? 0 : 2;
}
#endif
#endif
int main(int argc, char** argv) {
    log_init(nullptr);
#ifndef _WIN32
#ifdef LAMBDA_SECURITY_UPDATE_TESTING
    if (argc == 3 && !strcmp(argv[1], "--uri-profile")) {
        char output[96]; int size = snprintf(output, sizeof(output), "{\"role\":%s,\"wire\":%s,\"target\":%s}\n",
            https_url_opaque_name_valid(argv[2], 129) ? "true" : "false",
            https_url_public_path_valid(argv[2], 4096) ? "true" : "false",
            https_url_target_name_valid(argv[2], 4096) ? "true" : "false");
        return size > 0 && (size_t)size < sizeof(output) && write(1, output, (size_t)size) == size ? 0 : 2;
    }
    if (argc == 10 && !strcmp(argv[1], "--refresh-fixture")) return refresh_fixture_main(argv + 2);
    if (argc == 12 && !strcmp(argv[1], "--refresh-stage")) return refresh_fixture_main(argv + 2, atoi(argv[10]), argv[11]);
#endif
    if (argc == 9 && !strcmp(argv[1], "--pull-fixture")) return pull_fixture_main(argv + 2);
    if (argc == 3 && !strcmp(argv[1], "--production-peer")) {
        char url[128]; snprintf(url, sizeof(url), "https://127.0.0.1:%u/", (unsigned)strtoul(argv[2], nullptr, 10));
        HttpsPullRequest request = {url, 1024, 2000, nullptr, nullptr}; HttpsPullResponse response = {};
        HttpsPullStatus status = https_pull_once(&request, &response); mem_free(response.bytes);
        return status == HTTPS_PULL_FORBIDDEN_PEER ? 0 : 2;
    }
    if (argc == 2 && !strcmp(argv[1], "--system-roots")) {
        bool ordinary = trust_store_roots() != nullptr, system = trust_store_system_roots() != nullptr;
        const char* state = ordinary ? "ordinary_override_has_roots\n" : system ? "system_roots_only\n" : "system_roots_unavailable\n";
        (void)write(STDOUT_FILENO, state, strlen(state)); return !ordinary && system ? 0 : 2;
    }
    if (argc == 2 && !strcmp(argv[1], "--keylog-history")) {
        if (!curl_runtime_initialize()) return 2;
        unsetenv("SSLKEYLOGFILE");
        return curl_runtime_private_tls_ready() ? 2 : 0;
    }
#endif
    ::testing::InitGoogleTest(&argc, argv); return RUN_ALL_TESTS();
}
