#include "radiant.hpp"

#include "../lib/mem.h"
#include "../lib/url.h"

#include <string.h>
#include <stdio.h>
#include <unistd.h>

static bool radiant_resource_is_shared_res_href(const char* href) {
    return href && (strncmp(href, "res/", 4) == 0 ||
                    strncmp(href, "./res/", 6) == 0);
}

static bool radiant_resource_data_root_len(const char* local_path, size_t* out_len) {
    if (!local_path || !out_len) return false;

    const char* marker = strstr(local_path, "/layout/data/");
    if (marker) {
        *out_len = (size_t)(marker - local_path) + strlen("/layout/data");
        return true;
    }

    marker = strstr(local_path, "/test/layout/data/");
    if (marker) {
        *out_len = (size_t)(marker - local_path) + strlen("/test/layout/data");
        return true;
    }

    marker = strstr(local_path, "test/layout/data/");
    if (marker) {
        *out_len = (size_t)(marker - local_path) + strlen("test/layout/data");
        return true;
    }

    if (strncmp(local_path, "data/", 5) == 0) {
        *out_len = strlen("data");
        return true;
    }

    return false;
}

static char* radiant_resource_base_to_local_path(const char* base_path) {
    if (!base_path) return nullptr;

    if (strncmp(base_path, "file:", 5) == 0) {
        Url* base_url = url_parse(base_path);
        char* local = base_url ? url_to_local_path(base_url) : nullptr;
        if (base_url) url_destroy(base_url);
        return local;
    }

    return mem_strdup(base_path, MEM_CAT_LAYOUT);
}

bool radiant_resolve_shared_data_resource_path(const char* href, const char* base_path,
                                               char* out_path, size_t out_size) {
    if (!radiant_resource_is_shared_res_href(href) || !base_path ||
        !out_path || out_size == 0) {
        return false;
    }

    const char* rel_href = (href[0] == '.' && href[1] == '/') ? href + 2 : href;
    char* local_base = radiant_resource_base_to_local_path(base_path);
    if (!local_base) return false;

    size_t data_root_len = 0;
    bool found_root = radiant_resource_data_root_len(local_base, &data_root_len);
    if (!found_root) {
        mem_free(local_base);
        return false;
    }

    size_t href_len = strlen(rel_href);
    if (data_root_len + 1 + href_len + 1 > out_size) {
        mem_free(local_base);
        return false;
    }

    // Local file runs open category pages directly, but browser references serve
    // shared res/... assets from the layout/data root.
    memcpy(out_path, local_base, data_root_len);
    out_path[data_root_len] = '/';
    memcpy(out_path + data_root_len + 1, rel_href, href_len);
    out_path[data_root_len + 1 + href_len] = '\0';

    mem_free(local_base);
    return access(out_path, R_OK) == 0;
}

static bool radiant_resolve_wpt_root_resource_path(const char* href,
                                                   char* out_path, size_t out_size) {
    if (!href || !out_path || out_size == 0) return false;
    if (strlen("ref/wpt") + strlen(href) + 1 > out_size) return false;
    snprintf(out_path, out_size, "ref/wpt%s", href);
    return access(out_path, R_OK) == 0;
}

bool radiant_resolve_layout_support_resource_path(const char* href, const char* base_path,
                                                  char* out_path, size_t out_size) {
    if (!href || href[0] != '/' || href[1] == '/' || !base_path ||
        !out_path || out_size == 0) return false;

    const char* base_local = base_path;
    if (strncmp(base_local, "file:", 5) == 0) {
        base_local += 5;
        if (base_local[0] == '/' && base_local[1] == '/') base_local += 2;
    }

    const char* data_marker = strstr(base_local, "/data/");
    size_t marker_prefix_len = 1;
    if (!data_marker && strncmp(base_local, "data/", 5) == 0) {
        data_marker = base_local;
        marker_prefix_len = 0;
    }
    if (!data_marker) {
        data_marker = strstr(base_local, "test/layout/data/");
        marker_prefix_len = 0;
    }
    if (!data_marker) {
        if (strlen("test/layout/data/support") + strlen(href) + 1 > out_size) return false;
        snprintf(out_path, out_size, "test/layout/data/support%s", href);
        if (access(out_path, R_OK) == 0) return true;
        return radiant_resolve_wpt_root_resource_path(href, out_path, out_size);
    }

    size_t data_root_len = data_marker - base_local + marker_prefix_len + strlen("data");
    if (data_root_len + strlen("/support") + strlen(href) + 1 > out_size) return false;
    memcpy(out_path, base_local, data_root_len);
    out_path[data_root_len] = '\0';
    strncat(out_path, "/support", out_size - strlen(out_path) - 1);
    strncat(out_path, href, out_size - strlen(out_path) - 1);
    if (access(out_path, R_OK) == 0) return true;
    return radiant_resolve_wpt_root_resource_path(href, out_path, out_size);
}
