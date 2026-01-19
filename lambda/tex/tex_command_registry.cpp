// tex_command_registry.cpp - Command registry implementation
//
// This implements the command registry for LaTeX package commands.

#include "tex_command_registry.hpp"
#include "lib/log.h"
#include <cstring>

namespace tex {

// ============================================================================
// Constructor
// ============================================================================

CommandRegistry::CommandRegistry(Arena* arena)
    : arena(arena)
    , cmd_count(0)
    , env_count(0)
    , current_scope(nullptr)
{
    // Initialize hash tables to nullptr
    memset(command_table, 0, sizeof(command_table));
    memset(environment_table, 0, sizeof(environment_table));
}

// ============================================================================
// Hash Function
// ============================================================================

size_t CommandRegistry::hash_name(const char* name) const {
    if (!name) return 0;
    
    // Simple FNV-1a hash
    size_t hash = 14695981039346656037ULL;
    for (const char* p = name; *p; p++) {
        hash ^= (unsigned char)*p;
        hash *= 1099511628211ULL;
    }
    return hash % HASH_SIZE;
}

// ============================================================================
// Memory Allocation Helpers
// ============================================================================

const char* CommandRegistry::alloc_string(const char* str) {
    if (!str) return nullptr;
    size_t len = strlen(str);
    char* copy = (char*)arena_alloc(arena, len + 1);
    memcpy(copy, str, len + 1);
    return copy;
}

CommandDef* CommandRegistry::alloc_command_def() {
    CommandDef* def = (CommandDef*)arena_alloc(arena, sizeof(CommandDef));
    memset(def, 0, sizeof(CommandDef));
    return def;
}

EnvironmentDef* CommandRegistry::alloc_environment_def() {
    EnvironmentDef* def = (EnvironmentDef*)arena_alloc(arena, sizeof(EnvironmentDef));
    memset(def, 0, sizeof(EnvironmentDef));
    return def;
}

// ============================================================================
// Command Registration
// ============================================================================

void CommandRegistry::define_macro(const char* name, const char* params,
                                    const char* replacement) {
    if (!name) return;
    
    CommandDef* def = alloc_command_def();
    def->name = alloc_string(name);
    def->type = CommandType::MACRO;
    def->params = alloc_string(params);
    def->replacement = alloc_string(replacement);
    
    // Insert into hash table
    size_t idx = hash_name(name);
    def->next = command_table[idx];
    command_table[idx] = def;
    cmd_count++;
    
    log_debug("command_registry: defined macro '%s'", name);
}

void CommandRegistry::define_constructor(const char* name, const char* params,
                                          const char* pattern) {
    if (!name) return;
    
    CommandDef* def = alloc_command_def();
    def->name = alloc_string(name);
    def->type = CommandType::CONSTRUCTOR;
    def->params = alloc_string(params);
    def->pattern = alloc_string(pattern);
    
    // Insert into hash table
    size_t idx = hash_name(name);
    def->next = command_table[idx];
    command_table[idx] = def;
    cmd_count++;
    
    log_debug("command_registry: defined constructor '%s'", name);
}

void CommandRegistry::define_primitive(const char* name, const char* params) {
    if (!name) return;
    
    CommandDef* def = alloc_command_def();
    def->name = alloc_string(name);
    def->type = CommandType::PRIMITIVE;
    def->params = alloc_string(params);
    
    // Insert into hash table
    size_t idx = hash_name(name);
    def->next = command_table[idx];
    command_table[idx] = def;
    cmd_count++;
    
    log_debug("command_registry: defined primitive '%s'", name);
}

void CommandRegistry::define_callback(const char* name, const char* params,
                                       CommandCallback callback) {
    if (!name) return;
    
    CommandDef* def = alloc_command_def();
    def->name = alloc_string(name);
    def->type = CommandType::CALLBACK;
    def->params = alloc_string(params);
    def->callback = callback;
    
    // Insert into hash table
    size_t idx = hash_name(name);
    def->next = command_table[idx];
    command_table[idx] = def;
    cmd_count++;
    
    log_debug("command_registry: defined callback '%s'", name);
}

void CommandRegistry::define_math(const char* name, const char* meaning,
                                   const char* role) {
    if (!name) return;
    
    CommandDef* def = alloc_command_def();
    def->name = alloc_string(name);
    def->type = CommandType::MATH;
    def->is_math = true;
    def->replacement = alloc_string(meaning);
    def->description = alloc_string(role);
    
    // Insert into hash table
    size_t idx = hash_name(name);
    def->next = command_table[idx];
    command_table[idx] = def;
    cmd_count++;
    
    log_debug("command_registry: defined math '%s'", name);
}

void CommandRegistry::define_environment(const char* name, const char* params,
                                          const char* begin_pattern,
                                          const char* end_pattern,
                                          bool is_math) {
    if (!name) return;
    
    EnvironmentDef* def = alloc_environment_def();
    def->name = alloc_string(name);
    def->params = alloc_string(params);
    def->begin_pattern = alloc_string(begin_pattern);
    def->end_pattern = alloc_string(end_pattern);
    def->is_math = is_math;
    
    // Insert into hash table
    size_t idx = hash_name(name);
    def->next = environment_table[idx];
    environment_table[idx] = def;
    env_count++;
    
    log_debug("command_registry: defined environment '%s'", name);
}

// ============================================================================
// Command Lookup
// ============================================================================

const CommandDef* CommandRegistry::lookup(const char* name) const {
    if (!name) return nullptr;
    
    // First check local scope
    for (Scope* scope = current_scope; scope; scope = scope->parent) {
        for (CommandDef* def = scope->local_commands; def; def = def->next) {
            if (strcmp(def->name, name) == 0) {
                return def;
            }
        }
    }
    
    // Then check global table
    size_t idx = hash_name(name);
    for (CommandDef* def = command_table[idx]; def; def = def->next) {
        if (strcmp(def->name, name) == 0) {
            return def;
        }
    }
    
    return nullptr;
}

const EnvironmentDef* CommandRegistry::lookup_environment(const char* name) const {
    if (!name) return nullptr;
    
    // First check local scope
    for (Scope* scope = current_scope; scope; scope = scope->parent) {
        for (EnvironmentDef* def = scope->local_environments; def; def = def->next) {
            if (strcmp(def->name, name) == 0) {
                return def;
            }
        }
    }
    
    // Then check global table
    size_t idx = hash_name(name);
    for (EnvironmentDef* def = environment_table[idx]; def; def = def->next) {
        if (strcmp(def->name, name) == 0) {
            return def;
        }
    }
    
    return nullptr;
}

bool CommandRegistry::has_command(const char* name) const {
    return lookup(name) != nullptr;
}

bool CommandRegistry::has_environment(const char* name) const {
    return lookup_environment(name) != nullptr;
}

// ============================================================================
// Scoping
// ============================================================================

void CommandRegistry::begin_group() {
    Scope* scope = (Scope*)arena_alloc(arena, sizeof(Scope));
    scope->local_commands = nullptr;
    scope->local_environments = nullptr;
    scope->parent = current_scope;
    current_scope = scope;
    
    log_debug("command_registry: begin_group");
}

void CommandRegistry::end_group() {
    if (current_scope) {
        current_scope = current_scope->parent;
        log_debug("command_registry: end_group");
    }
}

void CommandRegistry::make_global(const char* name) {
    // Find command in current scope and move to global table
    if (!current_scope || !name) return;
    
    CommandDef* prev = nullptr;
    for (CommandDef* def = current_scope->local_commands; def; def = def->next) {
        if (strcmp(def->name, name) == 0) {
            // Remove from local scope
            if (prev) {
                prev->next = def->next;
            } else {
                current_scope->local_commands = def->next;
            }
            
            // Add to global table
            size_t idx = hash_name(name);
            def->next = command_table[idx];
            command_table[idx] = def;
            
            log_debug("command_registry: made '%s' global", name);
            return;
        }
        prev = def;
    }
}

} // namespace tex
