#include "radiant.hpp"

#include "../lib/mem.h"
#include "../lib/url.h"
#include "../lib/file.h"
#include "../lambda/input/css/css_parser.hpp"

#include <string.h>
#include <stdio.h>
#include <unistd.h>

static void radiant_visit_css_rule(CssRule* rule,
                                   RadiantCssDeclarationVisitor visitor,
                                   void* context) {
    if (!rule || !visitor) return;
    if (rule->type == CSS_RULE_STYLE) {
        for (size_t i = 0; i < rule->data.style_rule.declaration_count; i++) {
            visitor(rule->data.style_rule.declarations[i], context);
        }
        for (size_t i = 0; i < rule->data.style_rule.nested_rule_count; i++) {
            radiant_visit_css_rule(rule->data.style_rule.nested_rules[i],
                                   visitor, context);
        }
    } else if (rule->type == CSS_RULE_MEDIA || rule->type == CSS_RULE_SUPPORTS ||
               rule->type == CSS_RULE_CONTAINER || rule->type == CSS_RULE_SCOPE ||
               rule->type == CSS_RULE_LAYER) {
        for (size_t i = 0; i < rule->data.conditional_rule.rule_count; i++) {
            radiant_visit_css_rule(rule->data.conditional_rule.rules[i],
                                   visitor, context);
        }
    }
}

void radiant_for_each_css_declaration(CssStylesheet* stylesheet,
                                      RadiantCssDeclarationVisitor visitor,
                                      void* context) {
    if (!stylesheet || !visitor) return;
    for (size_t i = 0; i < stylesheet->rule_count; i++) {
        radiant_visit_css_rule(stylesheet->rules[i], visitor, context);
    }
}

bool radiant_url_is_http(const char* url) {
    return url && (strncmp(url, "http://", 7) == 0 ||
                   strncmp(url, "https://", 8) == 0);
}

bool radiant_url_is_http(const Url* url) {
    return url && (url->scheme == URL_SCHEME_HTTP ||
                   url->scheme == URL_SCHEME_HTTPS);
}

char* radiant_document_resource_base(DomDocument* doc, MemCategory category) {
    if (!doc || !doc->url) return nullptr;
    if (radiant_url_is_http(doc->url)) {
        const char* href = url_get_href(doc->url);
        return href ? mem_strdup(href, category) : nullptr;
    }
    char* local = url_to_local_path(doc->url);
    if (!local) return nullptr;
    char* result = mem_strdup(local, category);
    mem_free(local);
    return result;
}

static char* radiant_try_wpt_root(const char* root, const char* href,
                                  MemCategory category) {
    if (!root || !href || href[0] != '/') return nullptr;
    const char* candidates[2] = {href,
        strncmp(href, "/css/support/", 13) == 0 ? href + 13 : nullptr};
    for (int i = 0; i < 2; i++) {
        const char* path = candidates[i];
        if (!path || !*path) continue;
        size_t root_len = strlen(root);
        const char* query = strpbrk(path, "?#");
        size_t path_len = query ? (size_t)(query - path) : strlen(path);
        size_t separator = i == 0 ? 0 : 1;
        char* candidate = (char*)mem_alloc(root_len + path_len + separator + 1, category);
        if (!candidate) continue;
        memcpy(candidate, root, root_len);
        if (separator) candidate[root_len++] = '/';
        memcpy(candidate + root_len, path, path_len);
        candidate[root_len + path_len] = '\0';
        if (file_exists(candidate)) return candidate;
        mem_free(candidate);
    }
    return nullptr;
}

static char* radiant_try_wpt_prefix(const char* prefix, size_t prefix_len,
                                    const char* suffix, const char* href,
                                    MemCategory category) {
    if (!prefix || prefix_len == 0) return nullptr;
    size_t suffix_len = suffix ? strlen(suffix) : 0;
    char* root = (char*)mem_alloc(prefix_len + suffix_len + 1, category);
    if (!root) return nullptr;
    memcpy(root, prefix, prefix_len);
    if (suffix_len) memcpy(root + prefix_len, suffix, suffix_len);
    root[prefix_len + suffix_len] = '\0';
    char* result = radiant_try_wpt_root(root, href, category);
    mem_free(root);
    return result;
}

char* radiant_resolve_wpt_resource_path(const char* href, Url* base_url,
                                        MemCategory category) {
    if (!href || href[0] != '/' || href[1] == '/') return nullptr;

    if (base_url) {
        char* base_local = url_to_local_path(base_url);
        if (base_local) {
            const char* wpt_marker = strstr(base_local, "/ref/wpt/");
            if (wpt_marker) {
                size_t root_len = (size_t)(wpt_marker - base_local) + strlen("/ref/wpt");
                char* resolved = radiant_try_wpt_prefix(base_local, root_len,
                                                        nullptr, href, category);
                if (resolved) {
                    mem_free(base_local);
                    return resolved;
                }
            }
            char candidate[4096];
            if (radiant_resolve_layout_support_resource_path(
                    href, base_local, candidate, sizeof(candidate))) {
                char* resolved = mem_strdup(candidate, category);
                mem_free(base_local);
                return resolved;
            }
            mem_free(base_local);
        }
    }

    char cwd[4096];
    if (getcwd(cwd, sizeof(cwd))) {
        size_t cwd_len = strlen(cwd);
        char* resolved = radiant_try_wpt_prefix(cwd, cwd_len, "/ref/wpt",
                                                href, category);
        if (resolved) return resolved;
        resolved = radiant_try_wpt_prefix(cwd, cwd_len,
                                           "/test/layout/data/support", href,
                                           category);
        if (resolved) return resolved;
    }
    return radiant_try_wpt_root("ref/wpt", href, category);
}

char* radiant_resolve_resource_url(const char* href, Url* base_url,
                                   MemCategory category) {
    if (!href) return nullptr;
    if (url_is_absolute_url(href) || !base_url) {
        return mem_strdup(href, category);
    }
    Url* resolved = url_resolve_relative(href, base_url);
    if (resolved && resolved->href) {
        char* result = mem_strdup(resolved->href->chars, category);
        url_destroy(resolved);
        return result;
    }
    if (resolved) url_destroy(resolved);
    return mem_strdup(href, category);
}

char* radiant_resolve_resource_path(const char* href, const char* base_path,
                                    bool allow_fixture_root, MemCategory category) {
    if (!href || !*href) return nullptr;
    Url* base_url = base_path && *base_path ? url_parse(base_path) : nullptr;
    if (allow_fixture_root && href[0] == '/' && href[1] != '/' &&
        (!base_url || !radiant_url_is_http(base_path))) {
        char* fixture = radiant_resolve_wpt_resource_path(href, base_url, category);
        if (fixture) {
            if (base_url) url_destroy(base_url);
            return fixture;
        }
    }

    bool base_valid = base_url && base_url->is_valid;
    char* resolved = radiant_resolve_resource_url(href, base_url, category);
    if (base_url) url_destroy(base_url);
    if (!resolved) return nullptr;
    if (strncmp(resolved, "file:", 5) == 0) {
        Url* file_url = url_parse(resolved);
        char* local = file_url ? url_to_local_path(file_url) : nullptr;
        if (file_url) url_destroy(file_url);
        mem_free(resolved);
        if (local) {
            char* result = mem_strdup(local, category);
            mem_free(local);
            return result;
        }
        return nullptr;
    }
    if (base_path && !radiant_url_is_http(base_path) &&
        href[0] != '/' && !url_is_absolute_url(href) &&
        !base_valid) {
        const char* slash = strrchr(base_path, '/');
        if (slash) {
            size_t dir_len = (size_t)(slash - base_path) + 1;
            size_t href_len = strlen(href);
            char* joined = (char*)mem_alloc(dir_len + href_len + 1, category);
            if (joined) {
                memcpy(joined, base_path, dir_len);
                memcpy(joined + dir_len, href, href_len + 1);
                mem_free(resolved);
                return joined;
            }
        }
    }
    return resolved;
}

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

bool radiant_resolve_layout_relative_resource_path(const char* source_path,
                                                   const char* base_path,
                                                   char* out_path, size_t out_size) {
    if (!source_path || !base_path || !out_path || out_size == 0) return false;
    if (access(source_path, R_OK) == 0) return false;

    char* local_base = radiant_resource_base_to_local_path(base_path);
    if (!local_base) return false;

    size_t data_root_len = 0;
    if (!radiant_resource_data_root_len(local_base, &data_root_len)) {
        mem_free(local_base);
        return false;
    }

    // WPT mirrors keep HTML fixtures under layout/data while shared support
    // files remain under ref/wpt/css/<suite>; resolve the missing relative
    // source against that canonical tree before layout falls back to a system
    // resource with different metrics.
    const char* suite_start = local_base + data_root_len;
    if (*suite_start == '/') suite_start++;
    if (strncmp(suite_start, "wpt-", 4) != 0) {
        mem_free(local_base);
        return false;
    }
    const char* suite_end = strchr(suite_start, '/');
    if (!suite_end || suite_end == suite_start + 4) {
        mem_free(local_base);
        return false;
    }

    size_t source_prefix_len = data_root_len + 1 + (size_t)(suite_end - suite_start);
    if (strncmp(source_path, local_base, source_prefix_len) != 0 ||
        source_path[source_prefix_len] != '/') {
        mem_free(local_base);
        return false;
    }

    const char* relative_path = source_path + source_prefix_len + 1;
    size_t suite_name_len = (size_t)(suite_end - suite_start) - 4;
    size_t required = strlen("ref/wpt/css/") + suite_name_len + 1 +
        strlen(relative_path) + 1;
    if (required > out_size) {
        mem_free(local_base);
        return false;
    }

    // INT_CAST_OK: snprintf precision is an int string-length parameter.
    int written = snprintf(out_path, out_size, "ref/wpt/css/%.*s/%s",
                           (int)suite_name_len, suite_start + 4, relative_path);
    bool resolved = written > 0 && (size_t)written < out_size &&
        access(out_path, R_OK) == 0;
    mem_free(local_base);
    return resolved;
}
