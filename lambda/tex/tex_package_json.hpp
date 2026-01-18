// tex_package_json.hpp - JSON-based Package Loader
//
// Loads package definitions from .pkg.json files and registers
// commands with the CommandRegistry.
//
// This is a stepping stone before implementing Lambda Script packages.
//
// JSON Package Format:
// {
//   "name": "package_name",
//   "version": "1.0",
//   "requires": ["dependency1", "dependency2"],
//   "commands": {
//     "cmdname": { "type": "...", "params": "...", ... }
//   },
//   "environments": {
//     "envname": { "begin": "...", "end": "..." }
//   },
//   "math_symbols": {
//     "symbol": { "meaning": "...", "role": "..." }
//   }
// }

#ifndef TEX_PACKAGE_JSON_HPP
#define TEX_PACKAGE_JSON_HPP

#include "../lambda-data.hpp"
#include "../mark_reader.hpp"
#include "../../lib/arena.h"
#include <cstddef>

namespace tex {

// Forward declarations
class CommandRegistry;
struct CommandDef;
struct PackageSet;  // internal

// ============================================================================
// JSON Package Loader
// ============================================================================

class JsonPackageLoader {
public:
    JsonPackageLoader(CommandRegistry* registry, Arena* arena);
    ~JsonPackageLoader();
    
    // Load a package from JSON string
    bool load_from_string(const char* json_string, size_t len);
    
    // Load a package from file path
    bool load_from_file(const char* filepath);
    
    // Load a package by name (searches package directories)
    bool load_package(const char* name);
    
    // Add a search path for packages
    void add_search_path(const char* path);
    
    // Check if a package is loaded
    bool is_loaded(const char* name) const;
    
    // Get list of loaded packages
    const char** get_loaded_packages(size_t* count) const;
    
    // Error handling
    const char* get_last_error() const { return last_error; }
    
private:
    CommandRegistry* registry;
    Arena* arena;
    
    // Search paths for packages
    struct PathEntry {
        const char* path;
        PathEntry* next;
    };
    PathEntry* search_paths;
    
    // Loaded packages (simple set)
    void* loaded_packages;  // PackageSet*
    
    // Error message buffer
    char* last_error;
    
    // ========================================================================
    // Internal Parsing Helpers
    // ========================================================================
    
    bool parse_package(Item root);
    
    // Parse sections
    void parse_commands(MapReader& commands);
    void parse_environments(MapReader& environments);
    void parse_math_symbols(MapReader& symbols);
    void parse_math_operators(MapReader& operators);
    void parse_counters(MapReader& counters);
    void parse_delimiters(MapReader& delimiters);
    
    // Parse individual command definition
    bool parse_command(const char* name, MapReader& def);
    bool parse_environment(const char* name, MapReader& def);
    bool parse_math_symbol(const char* name, MapReader& def);
    bool parse_counter(const char* name, MapReader& def);
    
    // Load dependencies
    bool load_dependencies(ArrayReader& requires);
    
    // Helper to set error
    void set_error(const char* fmt, ...);
    
    // Helper to intern a string in the arena
    const char* intern_string(const char* str, size_t len);
    const char* intern_string(const char* str);
};

// ============================================================================
// Package Registry (Singleton-ish for Package Search Paths)
// ============================================================================

// Get the default package search paths
void get_default_package_paths(const char*** paths, size_t* count);

// Find a package file by name
const char* find_package_file(const char* name, const char** search_paths, size_t path_count);

} // namespace tex

#endif // TEX_PACKAGE_JSON_HPP
