// tex_command_registry.hpp - Command registry for LaTeX package system
//
// This provides a registry for LaTeX commands defined in package JSON files.
// Commands can be macros (text expansion), constructors (produce elements),
// primitives (side effects), environments, or math commands.

#ifndef TEX_COMMAND_REGISTRY_HPP
#define TEX_COMMAND_REGISTRY_HPP

#include "tex_document_model.hpp"
#include "../mark_reader.hpp"
#include "lib/arena.h"
#include "lib/strbuf.h"

namespace tex {

// Forward declarations
class TexDocumentModel;

// ============================================================================
// Command Types
// ============================================================================

// Command type from package JSON schema
enum class CommandType : uint8_t {
    MACRO,          // Simple text expansion (replacement text)
    PRIMITIVE,      // Side effect execution (no direct output)
    CONSTRUCTOR,    // Produces element for output (pattern-based)
    ENVIRONMENT,    // Begin/end pair
    MATH,           // Math-mode command
    CALLBACK,       // C++ callback function
};

// ============================================================================
// Command Callback Type
// ============================================================================

// Callback function type for complex commands that need C++ logic
using CommandCallback = DocElement* (*)(const ElementReader& elem,
                                         Arena* arena,
                                         TexDocumentModel* doc);

// ============================================================================
// Command Definition
// ============================================================================

// Command definition (parsed from JSON or registered programmatically)
struct CommandDef {
    const char* name;           // Command name (without backslash)
    CommandType type;           // Type of command
    
    // Parameter specification: "{}", "[]{}",  "[default]{}", etc.
    const char* params;
    
    // For MACRO: replacement text with #1, #2, etc.
    const char* replacement;
    
    // For CONSTRUCTOR: output pattern
    // e.g., "<frac><numer>#1</numer><denom>#2</denom></frac>"
    const char* pattern;
    
    // For CALLBACK: C++ callback function
    CommandCallback callback;
    
    // For ENVIRONMENT: begin/end patterns
    const char* begin_pattern;
    const char* end_pattern;
    
    // Math mode only?
    bool is_math;
    
    // Description (for documentation)
    const char* description;
    
    // Next in hash bucket (for collision handling)
    CommandDef* next;
};

// ============================================================================
// Environment Definition
// ============================================================================

// Environment definition for begin/end pairs
struct EnvironmentDef {
    const char* name;           // Environment name
    const char* begin_pattern;  // Pattern for \begin{env}
    const char* end_pattern;    // Pattern for \end{env} (usually empty)
    const char* params;         // Optional parameters after \begin{env}
    bool is_math;               // Math environment?
    const char* description;
    EnvironmentDef* next;
};

// ============================================================================
// Command Registry
// ============================================================================

class CommandRegistry {
public:
    explicit CommandRegistry(Arena* arena);
    ~CommandRegistry() = default;
    
    // ========================================================================
    // Command Registration
    // ========================================================================
    
    // Define a macro (simple text replacement)
    void define_macro(const char* name, const char* params, 
                      const char* replacement);
    
    // Define a constructor (produces output element)
    void define_constructor(const char* name, const char* params,
                            const char* pattern);
    
    // Define a primitive (side-effect only)
    void define_primitive(const char* name, const char* params);
    
    // Define a C++ callback handler
    void define_callback(const char* name, const char* params,
                         CommandCallback callback);
    
    // Define a math command
    void define_math(const char* name, const char* meaning,
                     const char* role);
    
    // Define an environment
    void define_environment(const char* name, const char* params,
                            const char* begin_pattern,
                            const char* end_pattern = nullptr,
                            bool is_math = false);
    
    // ========================================================================
    // Command Lookup
    // ========================================================================
    
    // Lookup a command by name
    const CommandDef* lookup(const char* name) const;
    
    // Lookup an environment by name
    const EnvironmentDef* lookup_environment(const char* name) const;
    
    // Check if a command exists
    bool has_command(const char* name) const;
    
    // Check if an environment exists
    bool has_environment(const char* name) const;
    
    // ========================================================================
    // Scoping (for group-local definitions)
    // ========================================================================
    
    // Begin a new scope (for { } grouping)
    void begin_group();
    
    // End current scope
    void end_group();
    
    // Make a command global (escape current scope)
    void make_global(const char* name);
    
    // ========================================================================
    // Statistics
    // ========================================================================
    
    // Get number of registered commands
    size_t command_count() const { return cmd_count; }
    
    // Get number of registered environments
    size_t environment_count() const { return env_count; }
    
private:
    Arena* arena;
    
    // Hash table size
    static constexpr size_t HASH_SIZE = 512;
    
    // Command hash table
    CommandDef* command_table[HASH_SIZE];
    size_t cmd_count;
    
    // Environment hash table
    EnvironmentDef* environment_table[HASH_SIZE];
    size_t env_count;
    
    // Scope stack for local definitions
    struct Scope {
        CommandDef* local_commands;
        EnvironmentDef* local_environments;
        Scope* parent;
    };
    Scope* current_scope;
    
    // Hash function for command names
    size_t hash_name(const char* name) const;
    
    // Allocate string in arena
    const char* alloc_string(const char* str);
    
    // Create a new command definition
    CommandDef* alloc_command_def();
    
    // Create a new environment definition
    EnvironmentDef* alloc_environment_def();
};

} // namespace tex

#endif // TEX_COMMAND_REGISTRY_HPP
