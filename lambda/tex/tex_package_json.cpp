// tex_package_json.cpp - JSON-based Package Loader Implementation
//
// Loads .pkg.json files and registers commands with the CommandRegistry.

#include "tex_package_json.hpp"
#include "../lambda-data.hpp"
#include "../mark_reader.hpp"
#include "../../lib/log.h"
#include "../../lib/strbuf.h"
#include "../../lib/mempool.h"
#include <cstdarg>
#include <cstdio>
#include <cstring>

// Forward declaration for JSON parser
void parse_json(Input* input, const char* json_string);

namespace tex {

// ============================================================================
// Simple String Set for tracking loaded packages
// ============================================================================

struct PackageSet {
    struct Entry {
        const char* name;
        Entry* next;
    };
    Entry* head;
    Arena* arena;
    
    PackageSet(Arena* a) : head(nullptr), arena(a) {}
    
    bool contains(const char* name) const {
        for (Entry* e = head; e; e = e->next) {
            if (strcmp(e->name, name) == 0) return true;
        }
        return false;
    }
    
    void insert(const char* name) {
        if (contains(name)) return;
        Entry* e = (Entry*)arena_alloc(arena, sizeof(Entry));
        size_t len = strlen(name);
        char* copy = (char*)arena_alloc(arena, len + 1);
        memcpy(copy, name, len + 1);
        e->name = copy;
        e->next = head;
        head = e;
    }
    
    size_t size() const {
        size_t count = 0;
        for (Entry* e = head; e; e = e->next) count++;
        return count;
    }
};

// ============================================================================
// JsonPackageLoader Implementation
// ============================================================================

JsonPackageLoader::JsonPackageLoader(CommandRegistry* reg, Arena* a)
    : registry(reg)
    , arena(a)
    , search_paths(nullptr)
    , loaded_packages(nullptr)
    , last_error(nullptr)
{
    loaded_packages = new (arena_alloc(arena, sizeof(PackageSet))) PackageSet(a);
    
    // add default search paths
    add_search_path("lambda/tex/packages");
    add_search_path("./packages");
}

JsonPackageLoader::~JsonPackageLoader() {
    // arena owns all memory
}

void JsonPackageLoader::add_search_path(const char* path) {
    PathEntry* entry = (PathEntry*)arena_alloc(arena, sizeof(PathEntry));
    entry->path = intern_string(path);
    entry->next = search_paths;
    search_paths = entry;
}

bool JsonPackageLoader::is_loaded(const char* name) const {
    return ((PackageSet*)loaded_packages)->contains(name);
}

const char** JsonPackageLoader::get_loaded_packages(size_t* count) const {
    PackageSet* set = (PackageSet*)loaded_packages;
    size_t n = set->size();
    
    if (count) *count = n;
    if (n == 0) return nullptr;
    
    const char** result = (const char**)arena_alloc(arena, sizeof(const char*) * n);
    size_t i = 0;
    for (PackageSet::Entry* e = set->head; e; e = e->next) {
        result[i++] = e->name;
    }
    
    return result;
}

// ============================================================================
// Loading Functions
// ============================================================================

bool JsonPackageLoader::load_from_string(const char* json_string, size_t len) {
    if (!json_string || len == 0) {
        set_error("empty json string");
        return false;
    }
    
    // create pool and input for parsing
    Pool* pool = pool_create();
    Input* input = Input::create(pool, nullptr);
    
    // null-terminated string for parser
    char* json_copy = (char*)arena_alloc(arena, len + 1);
    memcpy(json_copy, json_string, len);
    json_copy[len] = '\0';
    
    // parse_json is declared externally
    parse_json(input, json_copy);
    
    Item root = input->root;
    if (root.type_id() == LMD_TYPE_NULL || root.type_id() == LMD_TYPE_ERROR) {
        set_error("failed to parse JSON");
        pool_destroy(pool);
        return false;
    }
    
    bool success = parse_package(root);
    
    // don't destroy pool yet - we've copied strings to arena
    // pool_destroy(pool);  // TODO: copy all needed data first
    
    return success;
}

bool JsonPackageLoader::load_from_file(const char* filepath) {
    log_debug("package-json: loading from file '%s'", filepath);
    
    // read file contents
    FILE* f = fopen(filepath, "rb");
    if (!f) {
        set_error("failed to open file: %s", filepath);
        return false;
    }
    
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    if (size <= 0) {
        fclose(f);
        set_error("empty file: %s", filepath);
        return false;
    }
    
    char* buffer = (char*)arena_alloc(arena, size + 1);
    size_t read = fread(buffer, 1, size, f);
    fclose(f);
    
    buffer[read] = '\0';
    
    return load_from_string(buffer, read);
}

bool JsonPackageLoader::load_package(const char* name) {
    if (is_loaded(name)) {
        log_debug("package-json: '%s' already loaded", name);
        return true;
    }
    
    log_debug("package-json: searching for package '%s'", name);
    
    // build filename
    StrBuf* sb = strbuf_new();
    
    // search in paths
    for (PathEntry* path = search_paths; path; path = path->next) {
        strbuf_reset(sb);
        strbuf_append_str(sb, path->path);
        strbuf_append_char(sb, '/');
        strbuf_append_str(sb, name);
        strbuf_append_str(sb, ".pkg.json");
        
        // null-terminate for fopen
        strbuf_append_char(sb, '\0');
        const char* filepath = sb->str;
        
        FILE* f = fopen(filepath, "r");
        if (f) {
            fclose(f);
            // intern the filepath before sb is reset
            const char* interned_path = intern_string(filepath);
            strbuf_free(sb);
            log_debug("package-json: found '%s' at '%s'", name, interned_path);
            return load_from_file(interned_path);
        }
    }
    
    strbuf_free(sb);
    set_error("package not found: %s", name);
    return false;
}

// ============================================================================
// Package Parsing
// ============================================================================

bool JsonPackageLoader::parse_package(Item root) {
    ItemReader root_reader(root.to_const());
    
    if (!root_reader.isMap()) {
        set_error("package root must be an object");
        return false;
    }
    
    MapReader pkg = root_reader.asMap();
    
    // get package name
    ItemReader name_item = pkg.get("name");
    if (!name_item.isString()) {
        set_error("package missing 'name' field");
        return false;
    }
    const char* pkg_name = name_item.cstring();
    
    if (is_loaded(pkg_name)) {
        log_debug("package-json: '%s' already loaded (checked after parse)", pkg_name);
        return true;
    }
    
    log_info("package-json: loading package '%s'", pkg_name);
    
    // load dependencies first
    ItemReader requires_item = pkg.get("requires");
    if (requires_item.isArray()) {
        ArrayReader requires = requires_item.asArray();
        if (!load_dependencies(requires)) {
            return false;
        }
    }
    
    // mark as loaded before processing to prevent cycles
    ((PackageSet*)loaded_packages)->insert(pkg_name);
    
    // parse commands
    ItemReader commands_item = pkg.get("commands");
    if (commands_item.isMap()) {
        MapReader commands = commands_item.asMap();
        parse_commands(commands);
    }
    
    // parse environments
    ItemReader envs_item = pkg.get("environments");
    if (envs_item.isMap()) {
        MapReader envs = envs_item.asMap();
        parse_environments(envs);
    }
    
    // parse math_symbols
    ItemReader math_syms_item = pkg.get("math_symbols");
    if (math_syms_item.isMap()) {
        MapReader math_syms = math_syms_item.asMap();
        parse_math_symbols(math_syms);
    }
    
    // parse math_operators
    ItemReader math_ops_item = pkg.get("math_operators");
    if (math_ops_item.isMap()) {
        MapReader math_ops = math_ops_item.asMap();
        parse_math_operators(math_ops);
    }
    
    // parse counters
    ItemReader counters_item = pkg.get("counters");
    if (counters_item.isMap()) {
        MapReader counters = counters_item.asMap();
        parse_counters(counters);
    }
    
    // parse delimiters
    ItemReader delims_item = pkg.get("delimiters");
    if (delims_item.isMap()) {
        MapReader delims = delims_item.asMap();
        parse_delimiters(delims);
    }
    
    log_debug("package-json: successfully loaded '%s'", pkg_name);
    return true;
}

bool JsonPackageLoader::load_dependencies(ArrayReader& requires) {
    int64_t count = requires.length();
    for (int64_t i = 0; i < count; i++) {
        ItemReader dep = requires.get(i);
        if (!dep.isString()) continue;
        
        const char* dep_name = dep.cstring();
        if (!load_package(dep_name)) {
            // warning, not error - continue loading
            log_warn("package-json: failed to load dependency '%s'", dep_name);
        }
    }
    return true;
}

// ============================================================================
// Section Parsing
// ============================================================================

void JsonPackageLoader::parse_commands(MapReader& commands) {
    MapReader::EntryIterator iter = commands.entries();
    const char* key;
    ItemReader value;
    
    while (iter.next(&key, &value)) {
        if (!value.isMap()) {
            log_warn("package-json: command '%s' definition is not an object", key);
            continue;
        }
        MapReader def = value.asMap();
        parse_command(key, def);
    }
}

bool JsonPackageLoader::parse_command(const char* name, MapReader& def) {
    ItemReader type_item = def.get("type");
    if (!type_item.isString()) {
        log_warn("package-json: command '%s' missing 'type'", name);
        return false;
    }
    
    const char* type = type_item.cstring();
    const char* params = "";
    
    ItemReader params_item = def.get("params");
    if (params_item.isString()) {
        params = intern_string(params_item.cstring());
    }
    
    const char* interned_name = intern_string(name);
    
    if (strcmp(type, "macro") == 0) {
        ItemReader pattern_item = def.get("pattern");
        if (!pattern_item.isString()) {
            log_warn("package-json: macro '%s' missing 'pattern'", name);
            return false;
        }
        const char* pattern = intern_string(pattern_item.cstring());
        registry->define_macro(interned_name, params, pattern);
        log_debug("package-json: registered macro '%s'", name);
        
    } else if (strcmp(type, "primitive") == 0) {
        // primitives require callback - we'll skip for now since callbacks
        // need to be registered separately in C++
        ItemReader callback_item = def.get("callback");
        if (callback_item.isString()) {
            log_debug("package-json: primitive '%s' has callback '%s' (not yet implemented)", 
                     name, callback_item.cstring());
        }
        // register as a no-op primitive for now
        registry->define_constructor(interned_name, params, "");
        
    } else if (strcmp(type, "constructor") == 0) {
        ItemReader pattern_item = def.get("pattern");
        if (!pattern_item.isString()) {
            log_warn("package-json: constructor '%s' missing 'pattern'", name);
            return false;
        }
        const char* pattern = intern_string(pattern_item.cstring());
        registry->define_constructor(interned_name, params, pattern);
        log_debug("package-json: registered constructor '%s'", name);
        
    } else if (strcmp(type, "math") == 0) {
        ItemReader meaning_item = def.get("meaning");
        ItemReader role_item = def.get("role");
        
        const char* meaning = meaning_item.isString() ? meaning_item.cstring() : name;
        const char* role = role_item.isString() ? role_item.cstring() : "ORDINARY";
        
        registry->define_math(interned_name, intern_string(meaning), intern_string(role));
        log_debug("package-json: registered math symbol '%s'", name);
        
    } else {
        log_warn("package-json: unknown command type '%s' for '%s'", type, name);
        return false;
    }
    
    return true;
}

void JsonPackageLoader::parse_environments(MapReader& environments) {
    MapReader::EntryIterator iter = environments.entries();
    const char* key;
    ItemReader value;
    
    while (iter.next(&key, &value)) {
        if (!value.isMap()) {
            log_warn("package-json: environment '%s' definition is not an object", key);
            continue;
        }
        MapReader def = value.asMap();
        parse_environment(key, def);
    }
}

bool JsonPackageLoader::parse_environment(const char* name, MapReader& def) {
    ItemReader begin_item = def.get("begin");
    ItemReader end_item = def.get("end");
    
    if (!begin_item.isString() || !end_item.isString()) {
        log_warn("package-json: environment '%s' missing 'begin' or 'end'", name);
        return false;
    }
    
    const char* interned_name = intern_string(name);
    const char* begin_pattern = intern_string(begin_item.cstring());
    const char* end_pattern = intern_string(end_item.cstring());
    
    registry->define_environment(interned_name, begin_pattern, end_pattern);
    log_debug("package-json: registered environment '%s'", name);
    
    return true;
}

void JsonPackageLoader::parse_math_symbols(MapReader& symbols) {
    MapReader::EntryIterator iter = symbols.entries();
    const char* key;
    ItemReader value;
    
    while (iter.next(&key, &value)) {
        if (!value.isMap()) {
            log_warn("package-json: math_symbol '%s' definition is not an object", key);
            continue;
        }
        MapReader def = value.asMap();
        parse_math_symbol(key, def);
    }
}

bool JsonPackageLoader::parse_math_symbol(const char* name, MapReader& def) {
    ItemReader meaning_item = def.get("meaning");
    ItemReader role_item = def.get("role");
    
    const char* meaning = meaning_item.isString() ? meaning_item.cstring() : name;
    const char* role = role_item.isString() ? role_item.cstring() : "ORDINARY";
    
    const char* interned_name = intern_string(name);
    registry->define_math(interned_name, intern_string(meaning), intern_string(role));
    log_debug("package-json: registered math symbol '%s' = '%s'", name, meaning);
    
    return true;
}

void JsonPackageLoader::parse_math_operators(MapReader& operators) {
    MapReader::EntryIterator iter = operators.entries();
    const char* key;
    ItemReader value;
    
    while (iter.next(&key, &value)) {
        if (!value.isMap()) continue;
        
        MapReader def = value.asMap();
        ItemReader meaning_item = def.get("meaning");
        ItemReader role_item = def.get("role");
        
        const char* meaning = meaning_item.isString() ? meaning_item.cstring() : key;
        const char* role = role_item.isString() ? role_item.cstring() : "FUNCTION";
        
        const char* interned_name = intern_string(key);
        registry->define_math(interned_name, intern_string(meaning), intern_string(role));
        log_debug("package-json: registered math operator '%s'", key);
    }
}

void JsonPackageLoader::parse_counters(MapReader& counters) {
    // counters are handled by digester, not command registry
    // for now, just log them
    MapReader::EntryIterator iter = counters.entries();
    const char* key;
    ItemReader value;
    
    while (iter.next(&key, &value)) {
        log_debug("package-json: counter '%s' defined (not yet implemented)", key);
    }
}

void JsonPackageLoader::parse_delimiters(MapReader& delimiters) {
    // delimiters are special - some are commands, some are size modifiers
    MapReader::EntryIterator iter = delimiters.entries();
    const char* key;
    ItemReader value;
    
    while (iter.next(&key, &value)) {
        if (!value.isMap()) continue;
        
        MapReader def = value.asMap();
        ItemReader type_item = def.get("type");
        
        if (type_item.isString() && strcmp(type_item.cstring(), "primitive") == 0) {
            // this is a \left, \right, \middle type command
            ItemReader callback_item = def.get("callback");
            if (callback_item.isString()) {
                log_debug("package-json: delimiter primitive '%s' with callback '%s' (not yet implemented)",
                         key, callback_item.cstring());
            }
            // register as constructor placeholder
            registry->define_constructor(intern_string(key), "{}", "");
        } else {
            // this is a size modifier like \big, \Big, etc.
            ItemReader size_item = def.get("size");
            if (size_item.isFloat() || size_item.isInt()) {
                // register as math symbol with size property
                log_debug("package-json: delimiter size modifier '%s'", key);
            }
        }
    }
}

// ============================================================================
// Helpers
// ============================================================================

const char* JsonPackageLoader::intern_string(const char* str, size_t len) {
    if (!str) return nullptr;
    if (len == 0) len = strlen(str);
    
    char* interned = (char*)arena_alloc(arena, len + 1);
    memcpy(interned, str, len);
    interned[len] = '\0';
    return interned;
}

const char* JsonPackageLoader::intern_string(const char* str) {
    return intern_string(str, str ? strlen(str) : 0);
}

void JsonPackageLoader::set_error(const char* fmt, ...) {
    char buffer[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    
    size_t len = strlen(buffer);
    last_error = (char*)arena_alloc(arena, len + 1);
    memcpy(last_error, buffer, len + 1);
    
    log_error("package-json: %s", last_error);
}

// ============================================================================
// Helper Functions
// ============================================================================

static const char* default_paths[] = {
    "lambda/tex/packages",
    "./packages",
    "/usr/share/lambda/packages",
};

void get_default_package_paths(const char*** paths, size_t* count) {
    *paths = default_paths;
    *count = sizeof(default_paths) / sizeof(default_paths[0]);
}

const char* find_package_file(const char* name, const char** search_paths, size_t path_count) {
    // TODO: implement file search
    return nullptr;
}

} // namespace tex
