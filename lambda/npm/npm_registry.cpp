// npm_registry.cpp — HTTP client for npm registry API

#include "npm_registry.h"
#include "semver.h"
#include "../lib/file.h"
#include "../lib/log.h"
#include "../lib/memtrack.h"
#include "../lib/mempool.h"
#include "../lambda-data.hpp"
#include "../mark_reader.hpp"
#include "../input/input.hpp"
// forward-declare JSON parser
Item parse_json_to_item(Input* input, const char* json_string);

// default npm registry
static const char* g_registry_url = "https://registry.npmjs.org";

// global cache directory
static const char* g_cache_dir = NULL;

static const char* get_cache_dir(void) {
    if (g_cache_dir) return g_cache_dir;
    // ~/.lambda/cache/npm
    const char* home = getenv("HOME");
    if (!home) home = "/tmp";  // fallback only for non-macOS
    static char cache_path[512];
    snprintf(cache_path, sizeof(cache_path), "%s/.lambda/cache/npm", home);
    file_ensure_dir(cache_path);
    return cache_path;
}

// ---------------------------------------------------------------------------
// HTTP fetch helper
// ---------------------------------------------------------------------------



static FetchResponse* registry_get(const char* url) {
    FetchConfig config = {};
    config.method = "GET";
    config.timeout_seconds = 30;
    config.verify_ssl = true;
    config.enable_compression = true;

    // npm registry requires Accept header for abbreviated metadata
    const char* headers[] = {
        "Accept: application/vnd.npm.install-v1+json",
        "User-Agent: lambda-npm/1.0"
    };
    config.headers = (char**)headers;
    config.header_count = 2;

    return http_fetch(url, &config);
}

// ---------------------------------------------------------------------------
// Parse registry JSON response
// ---------------------------------------------------------------------------

NpmRegistryPackage* npm_registry_fetch_package(const char* package_name) {
    if (!package_name || !*package_name) return NULL;

    // build URL: https://registry.npmjs.org/<package_name>
    char url[512];
    snprintf(url, sizeof(url), "%s/%s", g_registry_url, package_name);

    log_info("npm registry: fetching %s", url);

    FetchResponse* resp = registry_get(url);
    if (!resp || resp->status_code != 200) {
        log_error("npm registry: HTTP %ld for %s",
                  resp ? resp->status_code : 0, package_name);
        if (resp) free_fetch_response(resp);
        return NULL;
    }

    // parse JSON response
    Pool* pool = pool_create();
    Input* input = Input::create(pool);
    Item root = parse_json_to_item(input, resp->data);

    if (root.item == ITEM_NULL) {
        log_error("npm registry: failed to parse JSON for %s", package_name);
        free_fetch_response(resp);
        pool_destroy(pool);
        return NULL;
    }

    ItemReader root_reader(root.to_const());
    if (!root_reader.isMap()) {
        log_error("npm registry: unexpected response type for %s", package_name);
        free_fetch_response(resp);
        pool_destroy(pool);
        return NULL;
    }

    MapReader root_map = root_reader.asMap();

    // check for error
    ItemReader error_item = root_map.get("error");
    if (!error_item.isNull()) {
        log_error("npm registry: %s — %s", package_name, error_item.cstring());
        free_fetch_response(resp);
        pool_destroy(pool);
        return NULL;
    }

    NpmRegistryPackage* pkg = (NpmRegistryPackage*)mem_calloc(1, sizeof(NpmRegistryPackage), MEM_CAT_JS_RUNTIME);
    pkg->name = mem_strdup(package_name, MEM_CAT_JS_RUNTIME);

    // extract versions (keys of "versions" object)
    ItemReader versions_item = root_map.get("versions");
    if (versions_item.isMap()) {
        MapReader versions_map = versions_item.asMap();
        int n = (int)versions_map.size();
        pkg->version_strings = (char**)mem_calloc(n, sizeof(char*), MEM_CAT_JS_RUNTIME);
        auto keys = versions_map.keys();
        const char* key = NULL;
        int i = 0;
        while (keys.next(&key) && i < n) {
            pkg->version_strings[i] = mem_strdup(key, MEM_CAT_JS_RUNTIME);
            i++;
        }
        pkg->version_count = i;
    }

    // extract dist-tags.latest
    ItemReader dist_tags = root_map.get("dist-tags");
    if (dist_tags.isMap()) {
        MapReader dt_map = dist_tags.asMap();
        ItemReader latest = dt_map.get("latest");
        if (latest.isString()) {
            pkg->latest = mem_strdup(latest.cstring(), MEM_CAT_JS_RUNTIME);
        }
    }

    free_fetch_response(resp);
    pool_destroy(pool);
    return pkg;
}

NpmRegistryVersion* npm_registry_resolve_version(const char* package_name, const char* range_str) {
    if (!package_name || !*package_name) return NULL;

    // build URL for abbreviated metadata
    char url[512];
    snprintf(url, sizeof(url), "%s/%s", g_registry_url, package_name);

    log_info("npm registry: resolving %s@%s", package_name, range_str ? range_str : "latest");

    FetchResponse* resp = registry_get(url);
    if (!resp || resp->status_code != 200) {
        log_error("npm registry: HTTP %ld for %s",
                  resp ? resp->status_code : 0, package_name);
        if (resp) free_fetch_response(resp);
        return NULL;
    }

    // parse JSON
    Pool* pool = pool_create();
    Input* input = Input::create(pool);
    Item root = parse_json_to_item(input, resp->data);

    if (root.item == ITEM_NULL) {
        free_fetch_response(resp);
        pool_destroy(pool);
        return NULL;
    }

    MapReader root_map = MapReader::fromItem(root);

    // get versions object
    ItemReader versions_item = root_map.get("versions");
    if (!versions_item.isMap()) {
        free_fetch_response(resp);
        pool_destroy(pool);
        return NULL;
    }
    MapReader versions_map = versions_item.asMap();

    // parse the range
    SemVerRange range = semver_range_parse(range_str ? range_str : "*");
    if (!range.valid) {
        // try as exact version
        range = semver_range_parse(range_str);
    }

    // collect all versions, find best match
    int n = (int)versions_map.size();
    SemVer* semvers = (SemVer*)mem_calloc(n, sizeof(SemVer), MEM_CAT_JS_RUNTIME);
    char** ver_keys = (char**)mem_calloc(n, sizeof(char*), MEM_CAT_JS_RUNTIME);
    auto keys = versions_map.keys();
    const char* key = NULL;
    int count = 0;
    while (keys.next(&key) && count < n) {
        semvers[count] = semver_parse(key);
        ver_keys[count] = mem_strdup(key, MEM_CAT_JS_RUNTIME);
        count++;
    }

    int best = semver_max_satisfying(semvers, count, &range);
    if (best < 0) {
        log_error("npm registry: no version of %s satisfies %s", package_name, range_str);
        for (int i = 0; i < count; i++) mem_free(ver_keys[i]);
        mem_free(ver_keys);
        mem_free(semvers);
        free_fetch_response(resp);
        pool_destroy(pool);
        return NULL;
    }

    // get the version metadata
    const char* resolved_version = ver_keys[best];
    ItemReader ver_item = versions_map.get(resolved_version);

    NpmRegistryVersion* result = (NpmRegistryVersion*)mem_calloc(1, sizeof(NpmRegistryVersion), MEM_CAT_JS_RUNTIME);
    result->name = mem_strdup(package_name, MEM_CAT_JS_RUNTIME);
    result->version = mem_strdup(resolved_version, MEM_CAT_JS_RUNTIME);

    if (ver_item.isMap()) {
        MapReader ver_map = ver_item.asMap();

        // dist.tarball and dist.integrity
        ItemReader dist_item = ver_map.get("dist");
        if (dist_item.isMap()) {
            MapReader dist_map = dist_item.asMap();
            ItemReader tarball = dist_map.get("tarball");
            if (tarball.isString()) {
                result->tarball_url = mem_strdup(tarball.cstring(), MEM_CAT_JS_RUNTIME);
            }
            ItemReader integrity = dist_map.get("integrity");
            if (integrity.isString()) {
                result->integrity = mem_strdup(integrity.cstring(), MEM_CAT_JS_RUNTIME);
            }
            // fallback to shasum if no integrity
            if (!result->integrity) {
                ItemReader shasum = dist_map.get("shasum");
                if (shasum.isString()) {
                    // construct integrity string from sha1
                    char buf[256];
                    snprintf(buf, sizeof(buf), "sha1-%s", shasum.cstring());
                    result->integrity = mem_strdup(buf, MEM_CAT_JS_RUNTIME);
                }
            }
        }

        // dependencies
        ItemReader deps_item = ver_map.get("dependencies");
        if (deps_item.isMap()) {
            MapReader deps_map = deps_item.asMap();
            int ndeps = (int)deps_map.size();
            result->dep_names = (char**)mem_calloc(ndeps, sizeof(char*), MEM_CAT_JS_RUNTIME);
            result->dep_ranges = (char**)mem_calloc(ndeps, sizeof(char*), MEM_CAT_JS_RUNTIME);
            auto dep_entries = deps_map.entries();
            const char* dk = NULL;
            ItemReader dv;
            int di = 0;
            while (dep_entries.next(&dk, &dv) && di < ndeps) {
                result->dep_names[di] = mem_strdup(dk, MEM_CAT_JS_RUNTIME);
                result->dep_ranges[di] = dv.cstring()
                    ? mem_strdup(dv.cstring(), MEM_CAT_JS_RUNTIME)
                    : mem_strdup("*", MEM_CAT_JS_RUNTIME);
                di++;
            }
            result->dep_count = di;
        }
    }

    // cleanup
    for (int i = 0; i < count; i++) mem_free(ver_keys[i]);
    mem_free(ver_keys);
    mem_free(semvers);
    free_fetch_response(resp);
    pool_destroy(pool);

    log_info("npm registry: resolved %s@%s → %s", package_name, range_str, result->version);
    return result;
}

// ---------------------------------------------------------------------------
// Tarball download
// ---------------------------------------------------------------------------

char* npm_registry_download_tarball(const char* tarball_url, const char* integrity) {
    if (!tarball_url) return NULL;

    // use download_to_cache for caching
    const char* cache_dir = get_cache_dir();
    char* cache_path = NULL;
    char* content = download_to_cache(tarball_url, cache_dir, &cache_path);

    if (!content && !cache_path) {
        log_error("npm registry: failed to download %s", tarball_url);
        return NULL;
    }

    // if content was downloaded (not from cache), it's already written to cache_path
    if (content) mem_free(content);

    // TODO: verify integrity hash if provided

    log_info("npm registry: tarball cached at %s", cache_path);
    return cache_path;
}

// ---------------------------------------------------------------------------
// Free functions
// ---------------------------------------------------------------------------

void npm_registry_package_free(NpmRegistryPackage* pkg) {
    if (!pkg) return;
    if (pkg->name) mem_free(pkg->name);
    if (pkg->latest) mem_free(pkg->latest);
    for (int i = 0; i < pkg->version_count; i++) {
        if (pkg->version_strings[i]) mem_free(pkg->version_strings[i]);
    }
    if (pkg->version_strings) mem_free(pkg->version_strings);
    mem_free(pkg);
}

void npm_registry_version_free(NpmRegistryVersion* ver) {
    if (!ver) return;
    if (ver->name) mem_free(ver->name);
    if (ver->version) mem_free(ver->version);
    if (ver->tarball_url) mem_free(ver->tarball_url);
    if (ver->integrity) mem_free(ver->integrity);
    for (int i = 0; i < ver->dep_count; i++) {
        if (ver->dep_names[i]) mem_free(ver->dep_names[i]);
        if (ver->dep_ranges[i]) mem_free(ver->dep_ranges[i]);
    }
    if (ver->dep_names) mem_free(ver->dep_names);
    if (ver->dep_ranges) mem_free(ver->dep_ranges);
    mem_free(ver);
}
