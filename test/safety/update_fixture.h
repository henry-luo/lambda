#pragma once
#include "../../lambda/host/safety/update_controls.h"
#include <string.h>

static inline SecurityUpdatePackageSpec update_fixture_spec(const char* name) {
    SecurityUpdatePackageSpec value = {};
    strcpy(value.package, name); strcpy(value.metadata_base, "https://updates.example.org/repository/");
    strcpy(value.channel, "stable"); value.origin_count = 2;
    strcpy(value.origins[0], "https://updates.example.org"); strcpy(value.origins[1], "https://cdn.example.org");
    memset(value.publisher, 0x31, 32); memset(value.root, 0x42, 32);
    value.poll_seconds = 3600; value.job_seconds = 120; value.max_download_bytes = 1024 * 1024;
    return value;
}
