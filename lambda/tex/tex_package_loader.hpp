// tex_package_loader.hpp - Package loading system for LaTeX
//
// This provides functionality to load LaTeX package definitions from JSON files.
// Packages define commands, environments, and their behaviors.

#ifndef TEX_PACKAGE_LOADER_HPP
#define TEX_PACKAGE_LOADER_HPP

#include "tex_command_registry.hpp"
#include "lib/arena.h"
#include "lib/strbuf.h"

namespace tex {

// ============================================================================
// Package Loader
// ============================================================================

class PackageLoader {
public:
    PackageLoader(CommandRegistry* registry, Arena* arena);
    ~PackageLoader() = default;
    
    // ========================================================================
    // Package Loading
    // ========================================================================
    
    // Load base packages (tex_base, latex_base) - should be called first
    bool load_base_packages();
    
    // Load a document class (article, book, report, etc.)
    bool load_class(const char* class_name, const char* options = nullptr);
    
    // Load a package by name
    // Returns true if loaded successfully, false on error
    bool require_package(const char* pkg_name, const char* options = nullptr);
    
    // ========================================================================
    // Package Queries
    // ========================================================================
    
    // Check if a package is loaded
    bool is_loaded(const char* pkg_name) const;
    
    // Get list of loaded packages (for debugging)
    const char** get_loaded_packages(size_t* count) const;
    
    // ========================================================================
    // Search Paths
    // ========================================================================
    
    // Add a search path for package files
    void add_search_path(const char* path);
    
    // Set the default package directory
    void set_package_dir(const char* path);
    
    // ========================================================================
    // Error Handling
    // ========================================================================
    
    // Get last error message
    const char* get_last_error() const { return last_error; }
    
    // Clear error state
    void clear_error() { last_error = nullptr; }
    
private:
    CommandRegistry* registry;
    Arena* arena;
    
    // Default package directory (lambda/tex/packages/)
    const char* package_dir;
    
    // Loaded packages linked list
    struct LoadedPackage {
        const char* name;
        const char* version;
        LoadedPackage* next;
    };
    LoadedPackage* loaded_packages;
    size_t package_count;
    
    // Search paths linked list
    struct SearchPath {
        const char* path;
        SearchPath* next;
    };
    SearchPath* search_paths;
    
    // Error state
    const char* last_error;
    
    // ========================================================================
    // Internal Methods
    // ========================================================================
    
    // Find package file path
    const char* find_package_file(const char* pkg_name);
    
    // Load and parse a JSON package file
    bool load_json_package(const char* pkg_path);
    
    // Parse package JSON content
    bool parse_package_json(const char* json, size_t len, const char* pkg_name);
    
    // Parse commands section
    void parse_commands(const ItemReader& commands_map);
    
    // Parse environments section
    void parse_environments(const ItemReader& environments_map);
    
    // Parse math_symbols section
    void parse_math_symbols(const ItemReader& symbols_map);
    
    // Parse math_operators section  
    void parse_math_operators(const ItemReader& operators_map);
    
    // Parse counters section
    void parse_counters(const ItemReader& counters_map);
    
    // Parse delimiters section
    void parse_delimiters(const ItemReader& delimiters_map);
    
    // Parse a single command definition
    void parse_command_def(const char* cmd_name, const ItemReader& cmd_def);
    
    // Parse a single environment definition
    void parse_environment_def(const char* env_name, const ItemReader& env_def);
    
    // Handle package dependencies (requires field)
    bool load_dependencies(const ItemReader& requires_array);
    
    // Mark a package as loaded
    void mark_loaded(const char* pkg_name, const char* version);
    
    // Allocate string in arena
    const char* alloc_string(const char* str);
    
    // Set error message
    void set_error(const char* fmt, ...);
};

} // namespace tex

#endif // TEX_PACKAGE_LOADER_HPP
