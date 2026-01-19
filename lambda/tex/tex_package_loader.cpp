// tex_package_loader.cpp - Package loading implementation
//
// This implements loading LaTeX package definitions from JSON files.

#include "tex_package_loader.hpp"
#include "lib/log.h"
#include <cstring>
#include <cstdio>
#include <cstdarg>

// Include input system for JSON parsing
#include "../input/input.hpp"

// Forward declaration for JSON parsing (in global namespace)
void parse_json(Input* input, const char* json_string);

namespace tex {

// ============================================================================
// Constructor
// ============================================================================

PackageLoader::PackageLoader(CommandRegistry* registry, Arena* arena)
    : registry(registry)
    , arena(arena)
    , package_dir("lambda/tex/packages/")
    , loaded_packages(nullptr)
    , package_count(0)
    , search_paths(nullptr)
    , last_error(nullptr)
{
}

// ============================================================================
// String Allocation
// ============================================================================

const char* PackageLoader::alloc_string(const char* str) {
    if (!str) return nullptr;
    size_t len = strlen(str);
    char* copy = (char*)arena_alloc(arena, len + 1);
    memcpy(copy, str, len + 1);
    return copy;
}

// ============================================================================
// Error Handling
// ============================================================================

void PackageLoader::set_error(const char* fmt, ...) {
    char buffer[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    
    last_error = alloc_string(buffer);
    log_error("package_loader: %s", last_error);
}

// ============================================================================
// Package Tracking
// ============================================================================

void PackageLoader::mark_loaded(const char* pkg_name, const char* version) {
    LoadedPackage* pkg = (LoadedPackage*)arena_alloc(arena, sizeof(LoadedPackage));
    pkg->name = alloc_string(pkg_name);
    pkg->version = alloc_string(version);
    pkg->next = loaded_packages;
    loaded_packages = pkg;
    package_count++;
}

bool PackageLoader::is_loaded(const char* pkg_name) const {
    if (!pkg_name) return false;
    
    for (LoadedPackage* pkg = loaded_packages; pkg; pkg = pkg->next) {
        if (strcmp(pkg->name, pkg_name) == 0) {
            return true;
        }
    }
    return false;
}

const char** PackageLoader::get_loaded_packages(size_t* count) const {
    if (count) *count = package_count;
    
    if (package_count == 0) return nullptr;
    
    const char** names = (const char**)arena_alloc(arena, package_count * sizeof(const char*));
    size_t i = 0;
    for (LoadedPackage* pkg = loaded_packages; pkg; pkg = pkg->next) {
        names[i++] = pkg->name;
    }
    return names;
}

// ============================================================================
// Search Paths
// ============================================================================

void PackageLoader::add_search_path(const char* path) {
    if (!path) return;
    
    SearchPath* sp = (SearchPath*)arena_alloc(arena, sizeof(SearchPath));
    sp->path = alloc_string(path);
    sp->next = search_paths;
    search_paths = sp;
}

void PackageLoader::set_package_dir(const char* path) {
    package_dir = alloc_string(path);
}

const char* PackageLoader::find_package_file(const char* pkg_name) {
    if (!pkg_name) return nullptr;
    
    // Build path and check if file exists
    char path[512];
    
    // First check default package directory
    snprintf(path, sizeof(path), "%s%s.pkg.json", package_dir, pkg_name);
    FILE* f = fopen(path, "r");
    if (f) {
        fclose(f);
        return alloc_string(path);
    }
    
    // Check additional search paths
    for (SearchPath* sp = search_paths; sp; sp = sp->next) {
        snprintf(path, sizeof(path), "%s/%s.pkg.json", sp->path, pkg_name);
        f = fopen(path, "r");
        if (f) {
            fclose(f);
            return alloc_string(path);
        }
    }
    
    return nullptr;
}

// ============================================================================
// Package Loading
// ============================================================================

bool PackageLoader::load_base_packages() {
    log_info("package_loader: loading base packages");
    
    // Load tex_base first (TeX primitives)
    if (!require_package("tex_base")) {
        log_debug("package_loader: tex_base not available, continuing without it");
    }
    
    // Load latex_base (core LaTeX commands)
    if (!require_package("latex_base")) {
        log_debug("package_loader: latex_base not available, continuing without it");
    }
    
    return true;
}

bool PackageLoader::load_class(const char* class_name, const char* options) {
    (void)options;  // TODO: Handle class options
    
    if (!class_name) return false;
    
    log_info("package_loader: loading class '%s'", class_name);
    
    // Document classes are handled specially - they may define commands
    // For now, just require latex_base
    if (!is_loaded("latex_base")) {
        require_package("latex_base");
    }
    
    return true;
}

bool PackageLoader::require_package(const char* pkg_name, const char* options) {
    (void)options;  // TODO: Handle package options
    
    if (!pkg_name) return false;
    
    // Check if already loaded
    if (is_loaded(pkg_name)) {
        log_debug("package_loader: package '%s' already loaded", pkg_name);
        return true;
    }
    
    // Find package file
    const char* pkg_path = find_package_file(pkg_name);
    if (!pkg_path) {
        set_error("package '%s' not found", pkg_name);
        return false;
    }
    
    // Load the JSON file
    return load_json_package(pkg_path);
}

bool PackageLoader::load_json_package(const char* pkg_path) {
    if (!pkg_path) return false;
    
    log_debug("package_loader: loading '%s'", pkg_path);
    
    // Read file content
    FILE* f = fopen(pkg_path, "r");
    if (!f) {
        set_error("cannot open package file '%s'", pkg_path);
        return false;
    }
    
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    char* content = (char*)arena_alloc(arena, size + 1);
    size_t read_size = fread(content, 1, size, f);
    content[read_size] = '\0';
    fclose(f);
    
    // Extract package name from path for logging
    const char* pkg_name = strrchr(pkg_path, '/');
    pkg_name = pkg_name ? pkg_name + 1 : pkg_path;
    
    // Remove .pkg.json suffix for the name
    char name_buf[64];
    strncpy(name_buf, pkg_name, sizeof(name_buf) - 1);
    name_buf[sizeof(name_buf) - 1] = '\0';
    char* suffix = strstr(name_buf, ".pkg.json");
    if (suffix) *suffix = '\0';
    
    return parse_package_json(content, read_size, name_buf);
}

// ============================================================================
// JSON Parsing
// ============================================================================

// Helper to count parameters from param string like "[]{}",  "{}{}"
static int count_params(const char* params) {
    if (!params) return 0;
    int count = 0;
    for (const char* p = params; *p; p++) {
        if (*p == '{') count++;
    }
    return count;
}

bool PackageLoader::parse_package_json(const char* json, size_t len, const char* pkg_name) {
    (void)len;
    
    if (!json || !registry) return false;
    
    // Mark as loaded first to prevent circular dependencies
    mark_loaded(pkg_name, "1.0");
    
    log_info("package_loader: parsing package '%s'", pkg_name);
    
    // Create an Input for JSON parsing
    Input* input = InputManager::create_input(nullptr);
    if (!input) {
        set_error("failed to create input for package '%s'", pkg_name);
        return false;
    }
    
    // Parse JSON
    parse_json(input, json);
    
    // Check we got a valid map root
    if (get_type_id(input->root) != LMD_TYPE_MAP) {
        set_error("package '%s' root is not an object", pkg_name);
        return false;
    }
    
    // Walk the package structure
    ItemReader root(input->root.to_const());
    MapReader pkg = root.asMap();
    
    // Load dependencies first
    ItemReader requires_item = pkg.get("requires");
    if (requires_item.isList() || requires_item.isArray()) {
        load_dependencies(requires_item);
    }
    
    // Parse commands
    ItemReader commands_item = pkg.get("commands");
    if (commands_item.isMap()) {
        parse_commands(commands_item);
    }
    
    // Parse environments
    ItemReader envs_item = pkg.get("environments");
    if (envs_item.isMap()) {
        parse_environments(envs_item);
    }
    
    // Parse math_symbols
    ItemReader math_syms_item = pkg.get("math_symbols");
    if (math_syms_item.isMap()) {
        parse_math_symbols(math_syms_item);
    }
    
    // Parse math_operators
    ItemReader math_ops_item = pkg.get("math_operators");
    if (math_ops_item.isMap()) {
        parse_math_operators(math_ops_item);
    }
    
    // Parse counters
    ItemReader counters_item = pkg.get("counters");
    if (counters_item.isMap()) {
        parse_counters(counters_item);
    }
    
    // Parse delimiters
    ItemReader delims_item = pkg.get("delimiters");
    if (delims_item.isMap()) {
        parse_delimiters(delims_item);
    }
    
    log_info("package_loader: loaded package '%s' (%zu commands registered)", 
             pkg_name, registry->command_count());
    
    return true;
}

void PackageLoader::parse_commands(const ItemReader& commands_map) {
    // Walk through command definitions in the map
    if (!commands_map.isMap()) return;
    
    MapReader commands = commands_map.asMap();
    MapReader::EntryIterator iter = commands.entries();
    const char* cmd_name;
    ItemReader cmd_def;
    
    while (iter.next(&cmd_name, &cmd_def)) {
        parse_command_def(cmd_name, cmd_def);
    }
}

void PackageLoader::parse_command_def(const char* cmd_name, const ItemReader& cmd_def) {
    if (!cmd_name || !cmd_def.isMap()) return;
    
    MapReader def = cmd_def.asMap();
    
    // Get command type
    ItemReader type_item = def.get("type");
    if (!type_item.isString()) return;
    const char* type = type_item.cstring();
    
    // Get parameters
    ItemReader params_item = def.get("params");
    const char* params = params_item.isString() ? params_item.cstring() : "";
    
    // Get pattern/replacement
    ItemReader pattern_item = def.get("pattern");
    const char* pattern = pattern_item.isString() ? pattern_item.cstring() : "";
    
    ItemReader replacement_item = def.get("replacement");
    const char* replacement = replacement_item.isString() ? replacement_item.cstring() : "";
    
    // Register based on type
    if (strcmp(type, "macro") == 0) {
        registry->define_macro(cmd_name, params, replacement);
    } else if (strcmp(type, "constructor") == 0) {
        registry->define_constructor(cmd_name, params, pattern);
    } else if (strcmp(type, "primitive") == 0) {
        registry->define_primitive(cmd_name, params);
    } else if (strcmp(type, "math") == 0) {
        ItemReader meaning_item = def.get("meaning");
        const char* meaning = meaning_item.isString() ? meaning_item.cstring() : cmd_name;
        ItemReader role_item = def.get("role");
        const char* role = role_item.isString() ? role_item.cstring() : "";
        registry->define_math(cmd_name, meaning, role);
    }
    
    log_debug("package_loader: registered command '%s' (type=%s)", cmd_name, type);
}

void PackageLoader::parse_environments(const ItemReader& environments_map) {
    if (!environments_map.isMap()) return;
    
    MapReader environments = environments_map.asMap();
    MapReader::EntryIterator iter = environments.entries();
    const char* env_name;
    ItemReader env_def;
    
    while (iter.next(&env_name, &env_def)) {
        parse_environment_def(env_name, env_def);
    }
}

void PackageLoader::parse_environment_def(const char* env_name, const ItemReader& env_def) {
    if (!env_name || !env_def.isMap()) return;
    
    MapReader def = env_def.asMap();
    
    // Get parameters
    ItemReader params_item = def.get("params");
    const char* params = params_item.isString() ? params_item.cstring() : "";
    
    // Get begin/end patterns
    ItemReader begin_item = def.get("begin_pattern");
    const char* begin_pattern = begin_item.isString() ? begin_item.cstring() : "";
    
    ItemReader end_item = def.get("end_pattern");
    const char* end_pattern = end_item.isString() ? end_item.cstring() : "";
    
    // Check if math environment
    ItemReader mode_item = def.get("mode");
    bool is_math = mode_item.isString() && strcmp(mode_item.cstring(), "math") == 0;
    
    registry->define_environment(env_name, params, begin_pattern, end_pattern, is_math);
    
    log_debug("package_loader: registered environment '%s'", env_name);
}

bool PackageLoader::load_dependencies(const ItemReader& requires_array) {
    if (!requires_array.isList() && !requires_array.isArray()) return true;
    
    ArrayReader requires_list = requires_array.asArray();
    for (int64_t i = 0; i < requires_list.length(); i++) {
        ItemReader dep = requires_list.get(i);
        if (dep.isString()) {
            const char* dep_name = dep.cstring();
            if (!require_package(dep_name)) {
                log_error("package_loader: failed to load dependency '%s'", dep_name);
                // Don't fail completely - continue with other deps
            }
        }
    }
    
    return true;
}

// ============================================================================
// Math Symbols Parsing
// ============================================================================

void PackageLoader::parse_math_symbols(const ItemReader& symbols_map) {
    if (!symbols_map.isMap()) return;
    
    MapReader symbols = symbols_map.asMap();
    MapReader::EntryIterator iter = symbols.entries();
    const char* name;
    ItemReader def;
    
    while (iter.next(&name, &def)) {
        if (!def.isMap()) {
            log_debug("package_loader: math_symbol '%s' not an object, skipping", name);
            continue;
        }
        
        MapReader sym_def = def.asMap();
        ItemReader meaning_item = sym_def.get("meaning");
        ItemReader role_item = sym_def.get("role");
        
        const char* meaning = meaning_item.isString() ? meaning_item.cstring() : name;
        const char* role = role_item.isString() ? role_item.cstring() : "ORDINARY";
        
        registry->define_math(name, meaning, role);
        log_debug("package_loader: registered math symbol '%s' = '%s'", name, meaning);
    }
}

// ============================================================================
// Math Operators Parsing
// ============================================================================

void PackageLoader::parse_math_operators(const ItemReader& operators_map) {
    if (!operators_map.isMap()) return;
    
    MapReader operators = operators_map.asMap();
    MapReader::EntryIterator iter = operators.entries();
    const char* name;
    ItemReader def;
    
    while (iter.next(&name, &def)) {
        if (!def.isMap()) continue;
        
        MapReader op_def = def.asMap();
        ItemReader meaning_item = op_def.get("meaning");
        ItemReader role_item = op_def.get("role");
        
        const char* meaning = meaning_item.isString() ? meaning_item.cstring() : name;
        const char* role = role_item.isString() ? role_item.cstring() : "FUNCTION";
        
        registry->define_math(name, meaning, role);
        log_debug("package_loader: registered math operator '%s'", name);
    }
}

// ============================================================================
// Counters Parsing
// ============================================================================

void PackageLoader::parse_counters(const ItemReader& counters_map) {
    // Counters are handled by the document model, not command registry
    // For now, just log them for debugging
    if (!counters_map.isMap()) return;
    
    MapReader counters = counters_map.asMap();
    MapReader::EntryIterator iter = counters.entries();
    const char* name;
    ItemReader def;
    
    while (iter.next(&name, &def)) {
        log_debug("package_loader: counter '%s' defined (handled by doc model)", name);
    }
}

// ============================================================================
// Delimiters Parsing
// ============================================================================

void PackageLoader::parse_delimiters(const ItemReader& delimiters_map) {
    // Delimiters can be size modifiers (\big, \Big) or structural (\left, \right)
    if (!delimiters_map.isMap()) return;
    
    MapReader delimiters = delimiters_map.asMap();
    MapReader::EntryIterator iter = delimiters.entries();
    const char* name;
    ItemReader def;
    
    while (iter.next(&name, &def)) {
        if (!def.isMap()) continue;
        
        MapReader delim_def = def.asMap();
        ItemReader type_item = delim_def.get("type");
        
        if (type_item.isString() && strcmp(type_item.cstring(), "primitive") == 0) {
            // Structural delimiter like \left, \right, \middle
            ItemReader callback_item = delim_def.get("callback");
            if (callback_item.isString()) {
                log_debug("package_loader: delimiter primitive '%s' with callback '%s'",
                         name, callback_item.cstring());
            }
            // Register as constructor placeholder
            registry->define_constructor(name, "{}", "");
        } else {
            // Size modifier like \big, \Big, \bigg, etc.
            ItemReader size_item = delim_def.get("size");
            if (size_item.isFloat() || size_item.isInt()) {
                // Register as math command with size property
                double size = size_item.isFloat() ? size_item.asFloat() : (double)size_item.asInt();
                log_debug("package_loader: delimiter size modifier '%s' (size=%.2f)", name, size);
            }
            // Register as math symbol
            registry->define_math(name, name, "DELIMITER");
        }
    }
}

} // namespace tex
