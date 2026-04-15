// npm_package_json.cpp — Parse and query package.json files

#include "npm_package_json.h"
#include "../lib/file.h"
#include "../lib/log.h"
#include "../lib/memtrack.h"
#include "../lib/mempool.h"
#include "../lambda-data.hpp"
#include "../mark_reader.hpp"
// forward-declare JSON parser to avoid transitive input.hpp linkage issues
class Input;
Item parse_json_to_item(Input* input, const char* json_string);

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

// extract a string field from a MapReader, returning a mem_strdup'd copy
static const char* extract_string(MapReader& map, const char* key) {
    ItemReader val = map.get(key);
    const char* s = val.cstring();
    if (!s) return NULL;
    return mem_strdup(s, MEM_CAT_JS_RUNTIME);
}

// extract dependencies from a map field (each value is a version-range string)
static NpmDependency* extract_deps(MapReader& map, const char* field, int* out_count) {
    *out_count = 0;
    ItemReader dep_item = map.get(field);
    if (!dep_item.isMap()) return NULL;

    MapReader dep_map = dep_item.asMap();
    int n = (int)dep_map.size();
    if (n <= 0) return NULL;

    NpmDependency* deps = (NpmDependency*)mem_calloc(n, sizeof(NpmDependency), MEM_CAT_JS_RUNTIME);
    auto entries = dep_map.entries();
    const char* key = NULL;
    ItemReader val;
    int i = 0;
    while (entries.next(&key, &val) && i < n) {
        deps[i].name = mem_strdup(key, MEM_CAT_JS_RUNTIME);
        const char* v = val.cstring();
        deps[i].range = v ? mem_strdup(v, MEM_CAT_JS_RUNTIME) : mem_strdup("*", MEM_CAT_JS_RUNTIME);
        i++;
    }
    *out_count = i;
    return deps;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

NpmPackageJson* npm_package_json_parse_string(const char* json_content) {
    if (!json_content || !*json_content) return NULL;

    // create a temporary pool + Input for JSON parsing
    Pool* pool = pool_create();
    Input* input = Input::create(pool);

    Item root = parse_json_to_item(input, json_content);
    if (root.item == ITEM_NULL) {
        log_error("npm_package_json: failed to parse JSON");
        pool_destroy(pool);
        return NULL;
    }

    ItemReader root_reader(root.to_const());
    if (!root_reader.isMap()) {
        log_error("npm_package_json: root is not an object");
        pool_destroy(pool);
        return NULL;
    }

    NpmPackageJson* pkg = (NpmPackageJson*)mem_calloc(1, sizeof(NpmPackageJson), MEM_CAT_JS_RUNTIME);

    MapReader map = root_reader.asMap();

    // identity fields
    pkg->name    = extract_string(map, "name");
    pkg->version = extract_string(map, "version");

    // entry points
    pkg->main   = extract_string(map, "main");
    pkg->module = extract_string(map, "module");
    pkg->type   = extract_string(map, "type");
    if (!pkg->type) pkg->type = mem_strdup("commonjs", MEM_CAT_JS_RUNTIME);

    // metadata
    pkg->description = extract_string(map, "description");
    pkg->license     = extract_string(map, "license");

    // exports
    ItemReader exports_item = map.get("exports");
    if (!exports_item.isNull()) {
        pkg->has_exports = true;
        pkg->exports_item = (void*)(uintptr_t)exports_item.item().item;
    }

    // imports
    ItemReader imports_item = map.get("imports");
    if (!imports_item.isNull()) {
        pkg->has_imports = true;
        pkg->imports_item = (void*)(uintptr_t)imports_item.item().item;
    }

    // dependencies
    pkg->dependencies      = extract_deps(map, "dependencies", &pkg->dep_count);
    pkg->dev_dependencies  = extract_deps(map, "devDependencies", &pkg->dev_dep_count);
    pkg->peer_dependencies = extract_deps(map, "peerDependencies", &pkg->peer_dep_count);

    // scripts
    pkg->scripts = extract_deps(map, "scripts", &pkg->script_count);

    // bin — can be string or object
    ItemReader bin_item = map.get("bin");
    if (bin_item.isString()) {
        pkg->bin = (NpmDependency*)mem_calloc(1, sizeof(NpmDependency), MEM_CAT_JS_RUNTIME);
        pkg->bin[0].name = pkg->name ? mem_strdup(pkg->name, MEM_CAT_JS_RUNTIME) : mem_strdup("", MEM_CAT_JS_RUNTIME);
        pkg->bin[0].range = mem_strdup(bin_item.cstring(), MEM_CAT_JS_RUNTIME);
        pkg->bin_count = 1;
    } else if (bin_item.isMap()) {
        pkg->bin = extract_deps(map, "bin", &pkg->bin_count);
    }

    // keep raw item reference (note: pool must stay alive)
    pkg->raw_item = (void*)(uintptr_t)root.item;

    pkg->valid = true;

    // NOTE: we intentionally do NOT destroy the pool here, because extracted
    // string pointers (via cstring()) may point into pool memory. The pool
    // is kept alive. For the npm use case, package.json is parsed once and
    // the pool lifetime matches the process. For short-lived usage, the
    // mem_strdup'd fields are the safe copies.
    // Actually, since extract_string uses mem_strdup, the pool can be freed:
    pool_destroy(pool);

    return pkg;
}

NpmPackageJson* npm_package_json_parse(const char* file_path) {
    char* content = read_text_file(file_path);
    if (!content) {
        log_error("npm_package_json: cannot read '%s'", file_path);
        return NULL;
    }

    NpmPackageJson* pkg = npm_package_json_parse_string(content);
    mem_free(content);
    return pkg;
}

static void free_deps(NpmDependency* deps, int count) {
    if (!deps) return;
    for (int i = 0; i < count; i++) {
        if (deps[i].name)  mem_free((void*)deps[i].name);
        if (deps[i].range) mem_free((void*)deps[i].range);
    }
    mem_free(deps);
}

void npm_package_json_free(NpmPackageJson* pkg) {
    if (!pkg) return;

    if (pkg->name)        mem_free((void*)pkg->name);
    if (pkg->version)     mem_free((void*)pkg->version);
    if (pkg->main)        mem_free((void*)pkg->main);
    if (pkg->module)      mem_free((void*)pkg->module);
    if (pkg->type)        mem_free((void*)pkg->type);
    if (pkg->description) mem_free((void*)pkg->description);
    if (pkg->license)     mem_free((void*)pkg->license);

    free_deps(pkg->dependencies, pkg->dep_count);
    free_deps(pkg->dev_dependencies, pkg->dev_dep_count);
    free_deps(pkg->peer_dependencies, pkg->peer_dep_count);
    free_deps(pkg->scripts, pkg->script_count);
    free_deps(pkg->bin, pkg->bin_count);

    mem_free(pkg);
}

// ---------------------------------------------------------------------------
// Conditional exports resolution (Node.js package.json "exports" field)
// ---------------------------------------------------------------------------

// Resolve a single exports target value (may be string or object with conditions)
static const char* resolve_exports_target(Item target, const char* subpath,
                                          const char** conditions, int condition_count) {
    TypeId type = get_type_id(target);

    if (type == LMD_TYPE_STRING) {
        // direct string mapping
        return it2s(target)->chars;
    }

    if (type == LMD_TYPE_MAP) {
        // conditional object: { "import": "./esm/index.js", "require": "./cjs/index.js", "default": "./index.js" }
        MapReader map(it2map(target));
        // try each condition in priority order
        for (int i = 0; i < condition_count; i++) {
            ItemReader val = map.get(conditions[i]);
            if (!val.isNull()) {
                const char* result = resolve_exports_target(val.item(), subpath, conditions, condition_count);
                if (result) return result;
            }
        }
        return NULL;
    }

    if (type == LMD_TYPE_ARRAY) {
        // array of fallbacks: try each in order
        Array* arr = it2arr(target);
        for (int64_t i = 0; i < arr->length; i++) {
            const char* result = resolve_exports_target(arr->items[i], subpath, conditions, condition_count);
            if (result) return result;
        }
        return NULL;
    }

    return NULL;
}

const char* npm_resolve_exports(NpmPackageJson* pkg, const char* subpath,
                                const char** conditions, int condition_count) {
    if (!pkg || !pkg->has_exports) return NULL;

    // reconstruct the Item from stored pointer
    Item exports;
    exports.item = (uint64_t)(uintptr_t)pkg->exports_item;

    TypeId type = get_type_id(exports);

    // case 1: "exports": "./index.js" — single string
    if (type == LMD_TYPE_STRING) {
        if (!subpath || strcmp(subpath, ".") == 0 || subpath[0] == '\0') {
            return it2s(exports)->chars;
        }
        return NULL;
    }

    // case 2: "exports": { ".": ..., "./sub": ... }
    if (type == LMD_TYPE_MAP) {
        MapReader map(it2map(exports));

        // check if keys start with "." (subpath exports)
        ItemReader first_val = map.get(".");
        bool is_subpath_map = !first_val.isNull() || map.has("./");

        if (is_subpath_map) {
            // subpath exports map
            const char* path = (subpath && subpath[0]) ? subpath : ".";
            ItemReader target = map.get(path);
            if (target.isNull()) return NULL;
            return resolve_exports_target(target.item(), path, conditions, condition_count);
        } else {
            // conditional exports (root-level conditions)
            if (!subpath || strcmp(subpath, ".") == 0 || subpath[0] == '\0') {
                return resolve_exports_target(exports, ".", conditions, condition_count);
            }
            return NULL;
        }
    }

    return NULL;
}
