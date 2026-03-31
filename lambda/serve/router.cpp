/**
 * @file router.cpp
 * @brief Trie-based HTTP router implementation
 *
 * Supports Express-style route patterns:
 *   /users/:id          → named parameter
 *   /users/:id?         → optional parameter
 *   /files/*path        → wildcard catch-all
 *   /api/health         → exact match
 */

#include "router.hpp"
#include "serve_utils.hpp"
#include <string.h>
#include <stdlib.h>

// ============================================================================
// Internal helpers
// ============================================================================

// map HttpMethod bitmask to handler array index (bit position)
static int method_to_index(HttpMethod method) {
    switch (method) {
        case HTTP_GET:     return 0;
        case HTTP_POST:    return 1;
        case HTTP_PUT:     return 2;
        case HTTP_DELETE:  return 3;
        case HTTP_HEAD:    return 4;
        case HTTP_OPTIONS: return 5;
        case HTTP_PATCH:   return 6;
        case HTTP_CONNECT: return 7;
        case HTTP_TRACE:   return 8;
        default:           return -1;
    }
}

static RouteNode* route_node_create(const char *segment, int is_param,
                                     int is_wildcard, int is_optional) {
    RouteNode *node = (RouteNode *)serve_calloc(1, sizeof(RouteNode));
    if (!node) return NULL;
    node->segment = serve_strdup(segment);
    node->is_param = is_param;
    node->is_wildcard = is_wildcard;
    node->is_optional = is_optional;
    return node;
}

static void route_node_destroy(RouteNode *node) {
    if (!node) return;
    route_node_destroy(node->children);
    route_node_destroy(node->next);
    serve_free(node->segment);
    serve_free(node);
}

// find or create a child node matching the segment spec
static RouteNode* find_or_create_child(RouteNode *parent, const char *segment,
                                        int is_param, int is_wildcard, int is_optional) {
    // search existing children
    RouteNode *child = parent->children;
    while (child) {
        if (child->is_param == is_param && child->is_wildcard == is_wildcard) {
            if (is_param || is_wildcard) {
                // param/wildcard nodes match by type, not segment name
                return child;
            }
            if (strcmp(child->segment, segment) == 0) {
                return child;
            }
        }
        child = child->next;
    }

    // create new child
    RouteNode *new_child = route_node_create(segment, is_param, is_wildcard, is_optional);
    if (!new_child) return NULL;

    // insert: static children first (for matching priority), then params, then wildcards
    if (is_wildcard) {
        // wildcards go last
        RouteNode **pp = &parent->children;
        while (*pp) pp = &(*pp)->next;
        *pp = new_child;
    } else if (is_param) {
        // params go after static children
        RouteNode **pp = &parent->children;
        while (*pp && !(*pp)->is_param && !(*pp)->is_wildcard) pp = &(*pp)->next;
        new_child->next = *pp;
        *pp = new_child;
    } else {
        // static children go first
        new_child->next = parent->children;
        parent->children = new_child;
    }

    return new_child;
}

// parse a pattern like "/users/:id/posts/*rest" into segments
// each call returns the next segment and advances *pattern
static int next_segment(const char **pattern, char *buf, size_t bufsize,
                        int *out_param, int *out_wildcard, int *out_optional) {
    *out_param = 0;
    *out_wildcard = 0;
    *out_optional = 0;

    const char *p = *pattern;

    // skip leading slash
    while (*p == '/') p++;
    if (*p == '\0') return 0; // no more segments

    const char *start = p;

    if (*p == ':') {
        // named parameter :name or :name?
        *out_param = 1;
        p++; // skip ':'
        start = p;
        while (*p && *p != '/') p++;
        size_t len = (size_t)(p - start);
        // check for optional marker '?'
        if (len > 0 && start[len - 1] == '?') {
            *out_optional = 1;
            len--;
        }
        if (len >= bufsize) len = bufsize - 1;
        memcpy(buf, start, len);
        buf[len] = '\0';
    } else if (*p == '*') {
        // wildcard catch-all *name
        *out_wildcard = 1;
        p++; // skip '*'
        start = p;
        while (*p && *p != '/') p++;
        size_t len = (size_t)(p - start);
        if (len >= bufsize) len = bufsize - 1;
        memcpy(buf, start, len);
        buf[len] = '\0';
    } else {
        // static segment
        while (*p && *p != '/') p++;
        size_t len = (size_t)(p - start);
        if (len >= bufsize) len = bufsize - 1;
        memcpy(buf, start, len);
        buf[len] = '\0';
    }

    *pattern = p;
    return 1;
}

// ============================================================================
// Matching (recursive trie traversal)
// ============================================================================

// split path into segments array for matching
struct PathSegments {
    const char *parts[64];   // pointers into the path string
    int lengths[64];
    int count;
};

static void split_path(const char *path, PathSegments *segs) {
    segs->count = 0;
    const char *p = path;
    while (*p == '/') p++; // skip leading slashes

    while (*p && segs->count < 64) {
        const char *start = p;
        while (*p && *p != '/') p++;
        segs->parts[segs->count] = start;
        segs->lengths[segs->count] = (int)(p - start);
        segs->count++;
        while (*p == '/') p++;
    }
}

static RequestHandler match_node(RouteNode *node, PathSegments *segs, int seg_index,
                                  HttpMethod method, HttpHeader **out_params,
                                  void **out_user_data) {
    if (!node) return NULL;

    // try each child
    RouteNode *child = node->children;
    while (child) {
        if (child->is_wildcard) {
            // wildcard catches all remaining segments
            int idx = method_to_index(method);
            if (idx >= 0 && child->handlers[idx]) {
                // collect remaining path as wildcard value
                if (out_params && child->segment[0]) {
                    // build value from remaining segments
                    size_t total_len = 0;
                    for (int i = seg_index; i < segs->count; i++) {
                        if (i > seg_index) total_len++; // slash
                        total_len += segs->lengths[i];
                    }
                    char *val = (char *)serve_malloc(total_len + 1);
                    if (val) {
                        char *wp = val;
                        for (int i = seg_index; i < segs->count; i++) {
                            if (i > seg_index) *wp++ = '/';
                            memcpy(wp, segs->parts[i], segs->lengths[i]);
                            wp += segs->lengths[i];
                        }
                        *wp = '\0';
                        *out_params = http_header_add(*out_params, child->segment, val);
                        serve_free(val);
                    }
                }
                if (out_user_data) *out_user_data = child->handler_data[idx];
                return child->handlers[idx];
            }
        } else if (child->is_param) {
            if (seg_index < segs->count) {
                // parameter matches any single segment
                // try matching rest of path
                RequestHandler h = match_node(child, segs, seg_index + 1,
                                               method, out_params, out_user_data);
                if (h) {
                    // add param to output
                    if (out_params && child->segment[0]) {
                        char val[256];
                        int len = segs->lengths[seg_index];
                        if (len > 255) len = 255;
                        memcpy(val, segs->parts[seg_index], len);
                        val[len] = '\0';
                        *out_params = http_header_add(*out_params, child->segment, val);
                    }
                    return h;
                }
            }
            // optional parameter: also try matching without consuming this segment
            if (child->is_optional) {
                RequestHandler h = match_node(child, segs, seg_index,
                                               method, out_params, out_user_data);
                if (h) return h;
            }
        } else {
            // static segment: must match exactly
            if (seg_index < segs->count &&
                (int)strlen(child->segment) == segs->lengths[seg_index] &&
                memcmp(child->segment, segs->parts[seg_index], segs->lengths[seg_index]) == 0) {
                RequestHandler h = match_node(child, segs, seg_index + 1,
                                               method, out_params, out_user_data);
                if (h) return h;
            }
        }
        child = child->next;
    }

    // check if we've consumed all segments — look for handler on this node
    if (seg_index == segs->count) {
        int idx = method_to_index(method);
        if (idx >= 0 && node->handlers[idx]) {
            if (out_user_data) *out_user_data = node->handler_data[idx];
            return node->handlers[idx];
        }
    }

    return NULL;
}

// ============================================================================
// Public API
// ============================================================================

Router* router_create(const char *prefix) {
    Router *r = (Router *)serve_calloc(1, sizeof(Router));
    if (!r) return NULL;
    r->prefix = serve_strdup(prefix ? prefix : "");
    r->root = route_node_create("", 0, 0, 0);
    return r;
}

void router_destroy(Router *router) {
    if (!router) return;
    route_node_destroy(router->root);
    serve_free(router->prefix);
    serve_free(router);
}

int router_add(Router *router, HttpMethod method, const char *pattern,
               RequestHandler handler, void *user_data) {
    if (!router || !pattern || !handler) return -1;

    RouteNode *current = router->root;
    const char *p = pattern;
    char segment[256];
    int is_param, is_wildcard, is_optional;

    while (next_segment(&p, segment, sizeof(segment), &is_param, &is_wildcard, &is_optional)) {
        current = find_or_create_child(current, segment, is_param, is_wildcard, is_optional);
        if (!current) return -1;

        if (is_wildcard) break; // wildcard must be last segment
    }

    // register handler for each method bit
    for (int i = 0; i < 9; i++) {
        if (method & (1 << i)) {
            current->handlers[i] = handler;
            current->handler_data[i] = user_data;
        }
    }

    return 0;
}

// convenience methods
int router_get(Router *r, const char *pat, RequestHandler h, void *d)    { return router_add(r, HTTP_GET, pat, h, d); }
int router_post(Router *r, const char *pat, RequestHandler h, void *d)   { return router_add(r, HTTP_POST, pat, h, d); }
int router_put(Router *r, const char *pat, RequestHandler h, void *d)    { return router_add(r, HTTP_PUT, pat, h, d); }
int router_del(Router *r, const char *pat, RequestHandler h, void *d)    { return router_add(r, HTTP_DELETE, pat, h, d); }
int router_patch(Router *r, const char *pat, RequestHandler h, void *d)  { return router_add(r, HTTP_PATCH, pat, h, d); }
int router_options(Router *r, const char *pat, RequestHandler h, void *d){ return router_add(r, HTTP_OPTIONS, pat, h, d); }
int router_all(Router *r, const char *pat, RequestHandler h, void *d)    { return router_add(r, HTTP_ALL, pat, h, d); }

RequestHandler router_match(Router *router, HttpMethod method, const char *path,
                            HttpHeader **out_params, void **out_user_data) {
    if (!router || !path) return NULL;

    // strip router prefix if present
    const char *match_path = path;
    if (router->prefix && router->prefix[0]) {
        size_t prefix_len = strlen(router->prefix);
        if (strncmp(path, router->prefix, prefix_len) == 0) {
            match_path = path + prefix_len;
            if (*match_path == '\0') match_path = "/";
        } else {
            return NULL; // path doesn't match this router's prefix
        }
    }

    PathSegments segs;
    split_path(match_path, &segs);

    return match_node(router->root, &segs, 0, method, out_params, out_user_data);
}

int router_mount(Router *parent, Router *child) {
    if (!parent || !child) return -1;

    // prepend parent's prefix to child's prefix
    size_t plen = parent->prefix ? strlen(parent->prefix) : 0;
    size_t clen = child->prefix ? strlen(child->prefix) : 0;
    char *new_prefix = (char *)serve_malloc(plen + clen + 1);
    if (!new_prefix) return -1;

    if (plen > 0) memcpy(new_prefix, parent->prefix, plen);
    if (clen > 0) memcpy(new_prefix + plen, child->prefix, clen);
    new_prefix[plen + clen] = '\0';

    serve_free(child->prefix);
    child->prefix = new_prefix;

    return 0;
}
