#pragma once
#include "update_controls.h"
#include "lib/https_pull.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct SecurityUpdateDownload {
    HttpsPullResponse response;
    SecurityUpdateResult policy;
    uint32_t hops;
} SecurityUpdateDownload;
// Trusted updater only (D11.2.3-D11.2.4), never an app/worker request surface.
// public_path is a repository-relative public role/artifact name, not user
// content. A successful result is UNVERIFIED bytes; the caller must authenticate
// them before parsing artifacts, extracting, publishing or executing anything.
HttpsPullStatus security_update_download(SecurityUpdateJob* job, const char* public_path, SecurityUpdateDownload* out);
// The coordinator owns one aggregate body budget across all role/artifact pulls.
// Redirect bodies consume transfer_limit, independently of the signed 200 ceiling.
HttpsPullStatus security_update_download_limited(SecurityUpdateJob* job, const char* public_path,
    uint64_t transfer_limit, uint64_t success_limit, SecurityUpdateDownload* out);
#ifdef LAMBDA_HTTPS_PULL_TESTING
HttpsPullStatus security_update_download_fixture(SecurityUpdateJob* job, const char* public_path,
    const HttpsPullFixture* fixture, SecurityUpdateDownload* out);
HttpsPullStatus security_update_download_limited_fixture(SecurityUpdateJob* job, const char* public_path,
    uint64_t transfer_limit, uint64_t success_limit, const HttpsPullFixture* fixture, SecurityUpdateDownload* out);
#endif
void security_update_download_free(SecurityUpdateDownload* result);
#ifdef __cplusplus
}
#endif
