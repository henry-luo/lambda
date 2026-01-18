// tex_expander.cpp - TeX Expander (Gullet) Implementation
//
// Reference: TeXBook Chapter 20

#include "tex_expander.hpp"
#include "../../lib/log.h"
#include "../../lib/strbuf.h"
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cctype>

namespace tex {

// ============================================================================
// Primitive Helpers
// ============================================================================

bool is_expandable_primitive(PrimitiveType type) {
    switch (type) {
        // Expandable primitives
        case PrimitiveType::EXPANDAFTER:
        case PrimitiveType::NOEXPAND:
        case PrimitiveType::CSNAME:
        case PrimitiveType::STRING:
        case PrimitiveType::NUMBER:
        case PrimitiveType::ROMANNUMERAL:
        case PrimitiveType::THE:
        case PrimitiveType::MEANING:
        case PrimitiveType::JOBNAME:
        case PrimitiveType::UNEXPANDED:
        case PrimitiveType::DETOKENIZE:
        case PrimitiveType::NUMEXPR:
        // Conditionals are expandable
        case PrimitiveType::IF:
        case PrimitiveType::IFCAT:
        case PrimitiveType::IFX:
        case PrimitiveType::IFNUM:
        case PrimitiveType::IFDIM:
        case PrimitiveType::IFODD:
        case PrimitiveType::IFVMODE:
        case PrimitiveType::IFHMODE:
        case PrimitiveType::IFMMODE:
        case PrimitiveType::IFINNER:
        case PrimitiveType::IFVOID:
        case PrimitiveType::IFHBOX:
        case PrimitiveType::IFVBOX:
        case PrimitiveType::IFEOF:
        case PrimitiveType::IFTRUE:
        case PrimitiveType::IFFALSE:
        case PrimitiveType::IFCASE:
        case PrimitiveType::IFDEFINED:
        case PrimitiveType::IFCSNAME:
        case PrimitiveType::ELSE:
        case PrimitiveType::FI:
        case PrimitiveType::OR:
        // Definition commands - mark as "expandable" so our loop processes them
        case PrimitiveType::DEF:
        case PrimitiveType::EDEF:
        case PrimitiveType::GDEF:
        case PrimitiveType::XDEF:
        case PrimitiveType::LET:
        case PrimitiveType::FUTURELET:
        case PrimitiveType::GLOBAL:
        case PrimitiveType::LONG:
        case PrimitiveType::RELAX:
        case PrimitiveType::BEGINGROUP:
        case PrimitiveType::ENDGROUP:
        case PrimitiveType::BGROUP:
        case PrimitiveType::EGROUP:
        case PrimitiveType::NEWCOUNT:
            return true;
        default:
            return false;
    }
}

bool CommandEntry::is_expandable() const {
    switch (type) {
        case Type::UNDEFINED:
            return false;
        case Type::PRIMITIVE:
            return is_expandable_primitive(primitive);
        case Type::MACRO:
            return macro && macro->is_expandable && !macro->is_protected;
        case Type::CHAR_DEF:
        case Type::LET:
            return false;
        case Type::ACTIVE_CHAR:
            return true;  // Active chars are treated like macros
        default:
            return false;
    }
}

// ============================================================================
// Hash table helpers for commands
// ============================================================================

struct CommandHashEntry {
    const char* name;
    size_t name_len;
    CommandEntry entry;
};

static uint64_t cmd_hash(const void* item, uint64_t seed0, uint64_t seed1) {
    const CommandHashEntry* e = (const CommandHashEntry*)item;
    return hashmap_sip(e->name, e->name_len, seed0, seed1);
}

static int cmd_compare(const void* a, const void* b, void* udata) {
    const CommandHashEntry* ea = (const CommandHashEntry*)a;
    const CommandHashEntry* eb = (const CommandHashEntry*)b;
    if (ea->name_len != eb->name_len) return 1;
    return memcmp(ea->name, eb->name, ea->name_len);
}

// ============================================================================
// Expander Implementation
// ============================================================================

Expander::Expander(Arena* arena)
    : arena(arena)
    , tokenizer(arena)
    , commands(nullptr)
    , group_stack(nullptr)
    , cond_depth(0)
    , expansion_depth(0)
    , expansion_limit(1000)
    , currently_expanding(nullptr)
{
    commands = hashmap_new(sizeof(CommandHashEntry), 256, 0, 0,
                          cmd_hash, cmd_compare, nullptr, nullptr);
    
    memset(count_regs, 0, sizeof(count_regs));
    
    init_primitives();
}

Expander::~Expander() {
    if (commands) {
        hashmap_free(commands);
    }
    // Group stack cleaned up via arena
}

void Expander::push_input(const char* data, size_t len, const char* filename) {
    tokenizer.push_input(data, len, filename);
}

void Expander::push_tokens(TokenList* list) {
    tokenizer.push_tokens(list);
}

bool Expander::at_end() const {
    return tokenizer.at_end();
}

Token Expander::get_token() {
    return tokenizer.get_token();
}

void Expander::push_back(const Token& t) {
    tokenizer.push_back(t);
}

const CommandEntry* Expander::lookup(const char* name, size_t len) const {
    CommandHashEntry key = {name, len, {}};
    const CommandHashEntry* found = (const CommandHashEntry*)hashmap_get(commands, &key);
    return found ? &found->entry : nullptr;
}

const CommandEntry* Expander::lookup(const Token& t) const {
    if (t.type == TokenType::CS) {
        return lookup(t.cs.name, t.cs.len);
    }
    if (t.type == TokenType::CS_ACTIVE) {
        // Active character - look up as single-char name
        char name[2] = { t.chr.ch, '\0' };
        return lookup(name, 1);
    }
    return nullptr;
}

bool Expander::is_defined(const char* name, size_t len) const {
    const CommandEntry* entry = lookup(name, len);
    return entry && entry->type != CommandEntry::Type::UNDEFINED;
}

bool Expander::is_defined(const Token& t) const {
    const CommandEntry* entry = lookup(t);
    return entry && entry->type != CommandEntry::Type::UNDEFINED;
}

void Expander::register_primitive(const char* name, PrimitiveType type) {
    // Copy name to arena
    size_t len = strlen(name);
    char* name_copy = (char*)arena_alloc(arena, len + 1);
    memcpy(name_copy, name, len + 1);
    
    CommandHashEntry entry;
    entry.name = name_copy;
    entry.name_len = len;
    entry.entry.type = CommandEntry::Type::PRIMITIVE;
    entry.entry.primitive = type;
    
    hashmap_set(commands, &entry);
}

void Expander::init_primitives() {
    // Expansion primitives
    register_primitive("expandafter", PrimitiveType::EXPANDAFTER);
    register_primitive("noexpand", PrimitiveType::NOEXPAND);
    register_primitive("csname", PrimitiveType::CSNAME);
    register_primitive("endcsname", PrimitiveType::ENDCSNAME);
    register_primitive("string", PrimitiveType::STRING);
    register_primitive("number", PrimitiveType::NUMBER);
    register_primitive("romannumeral", PrimitiveType::ROMANNUMERAL);
    register_primitive("the", PrimitiveType::THE);
    register_primitive("meaning", PrimitiveType::MEANING);
    register_primitive("jobname", PrimitiveType::JOBNAME);
    
    // e-TeX primitives
    register_primitive("unexpanded", PrimitiveType::UNEXPANDED);
    register_primitive("detokenize", PrimitiveType::DETOKENIZE);
    register_primitive("numexpr", PrimitiveType::NUMEXPR);
    register_primitive("ifdefined", PrimitiveType::IFDEFINED);
    register_primitive("ifcsname", PrimitiveType::IFCSNAME);
    
    // Conditionals
    register_primitive("if", PrimitiveType::IF);
    register_primitive("ifcat", PrimitiveType::IFCAT);
    register_primitive("ifx", PrimitiveType::IFX);
    register_primitive("ifnum", PrimitiveType::IFNUM);
    register_primitive("ifdim", PrimitiveType::IFDIM);
    register_primitive("ifodd", PrimitiveType::IFODD);
    register_primitive("ifvmode", PrimitiveType::IFVMODE);
    register_primitive("ifhmode", PrimitiveType::IFHMODE);
    register_primitive("ifmmode", PrimitiveType::IFMMODE);
    register_primitive("ifinner", PrimitiveType::IFINNER);
    register_primitive("ifvoid", PrimitiveType::IFVOID);
    register_primitive("ifhbox", PrimitiveType::IFHBOX);
    register_primitive("ifvbox", PrimitiveType::IFVBOX);
    register_primitive("ifeof", PrimitiveType::IFEOF);
    register_primitive("iftrue", PrimitiveType::IFTRUE);
    register_primitive("iffalse", PrimitiveType::IFFALSE);
    register_primitive("ifcase", PrimitiveType::IFCASE);
    register_primitive("else", PrimitiveType::ELSE);
    register_primitive("fi", PrimitiveType::FI);
    register_primitive("or", PrimitiveType::OR);
    
    // Definitions
    register_primitive("def", PrimitiveType::DEF);
    register_primitive("edef", PrimitiveType::EDEF);
    register_primitive("gdef", PrimitiveType::GDEF);
    register_primitive("xdef", PrimitiveType::XDEF);
    register_primitive("let", PrimitiveType::LET);
    register_primitive("futurelet", PrimitiveType::FUTURELET);
    
    // Registers
    register_primitive("count", PrimitiveType::COUNT);
    register_primitive("dimen", PrimitiveType::DIMEN);
    register_primitive("skip", PrimitiveType::SKIP);
    register_primitive("toks", PrimitiveType::TOKS);
    register_primitive("advance", PrimitiveType::ADVANCE);
    register_primitive("multiply", PrimitiveType::MULTIPLY);
    register_primitive("divide", PrimitiveType::DIVIDE);
    register_primitive("newcount", PrimitiveType::NEWCOUNT);
    
    // Grouping
    register_primitive("begingroup", PrimitiveType::BEGINGROUP);
    register_primitive("endgroup", PrimitiveType::ENDGROUP);
    register_primitive("bgroup", PrimitiveType::BGROUP);
    register_primitive("egroup", PrimitiveType::EGROUP);
    register_primitive("global", PrimitiveType::GLOBAL);
    register_primitive("long", PrimitiveType::LONG);
    register_primitive("outer", PrimitiveType::OUTER);
    register_primitive("protected", PrimitiveType::PROTECTED);
    
    // Special
    register_primitive("relax", PrimitiveType::RELAX);
    register_primitive("catcode", PrimitiveType::CATCODE);
    register_primitive("lccode", PrimitiveType::LCCODE);
    register_primitive("uccode", PrimitiveType::UCCODE);
    register_primitive("mathcode", PrimitiveType::MATHCODE);
    register_primitive("endlinechar", PrimitiveType::ENDLINECHAR);
    register_primitive("escapechar", PrimitiveType::ESCAPECHAR);
    register_primitive("input", PrimitiveType::INPUT);
    register_primitive("endinput", PrimitiveType::ENDINPUT);
}

void Expander::init_latex_base() {
    // LaTeX definition commands (simplified - would need full implementation)
    register_primitive("newcommand", PrimitiveType::NEWCOMMAND);
    register_primitive("renewcommand", PrimitiveType::RENEWCOMMAND);
    register_primitive("providecommand", PrimitiveType::PROVIDECOMMAND);
}

// ============================================================================
// Token Expansion
// ============================================================================

Token Expander::expand_token() {
    while (true) {
        Token t = get_token();
        if (t.is_end()) return t;
        
        // Check noexpand flag first - return token as-is if marked
        if (t.noexpand) {
            t.noexpand = false;  // Clear flag so it's only one-shot
            return t;
        }
        
        // Handle grouping characters - update scope but still return them
        if (t.type == TokenType::CHAR) {
            if (t.catcode == CatCode::BEGIN_GROUP) {
                begin_group();
                return t;
            } else if (t.catcode == CatCode::END_GROUP) {
                end_group();
                return t;
            }
        }
        
        // Check if expandable
        const CommandEntry* entry = lookup(t);
        if (!entry || !entry->is_expandable()) {
            return t;  // Not expandable, return as-is
        }
        
        // Handle expansion
        if (entry->type == CommandEntry::Type::PRIMITIVE) {
            PrimitiveType prim = entry->primitive;
            
            // Handle expandable primitives
            switch (prim) {
                case PrimitiveType::EXPANDAFTER:
                    do_expandafter();
                    continue;
                    
                case PrimitiveType::NOEXPAND: {
                    // Return next token as unexpandable
                    Token next = get_token();
                    next.noexpand = true;  // Mark it as unexpandable
                    return next;
                }
                
                case PrimitiveType::CSNAME: {
                    TokenList result = do_csname();
                    if (!result.empty()) {
                        // Push result to be read
                        push_tokens(&result);
                    }
                    continue;
                }
                
                case PrimitiveType::STRING: {
                    Token next = get_token();
                    TokenList result = do_string(next);
                    push_tokens(&result);
                    continue;
                }
                
                case PrimitiveType::NUMBER: {
                    int n = scan_int();
                    TokenList result = do_number(n);
                    push_tokens(&result);
                    continue;
                }
                
                case PrimitiveType::ROMANNUMERAL: {
                    int n = scan_int();
                    TokenList result = do_romannumeral(n);
                    push_tokens(&result);
                    continue;
                }
                
                case PrimitiveType::THE: {
                    // \the<register>
                    Token reg_tok = get_token();
                    // Simplified: assume it's \count
                    int reg = scan_int();
                    TokenList result = do_number(count_regs[reg & 255]);
                    push_tokens(&result);
                    continue;
                }
                
                case PrimitiveType::MEANING: {
                    Token next = get_token();
                    TokenList result = do_meaning(next);
                    push_tokens(&result);
                    continue;
                }
                
                case PrimitiveType::UNEXPANDED: {
                    TokenList result = do_unexpanded();
                    push_tokens(&result);
                    continue;
                }
                
                case PrimitiveType::NUMEXPR: {
                    int n = do_numexpr();
                    TokenList result = do_number(n);
                    push_tokens(&result);
                    continue;
                }
                
                // Conditionals
                case PrimitiveType::IF:
                    do_if();
                    continue;
                case PrimitiveType::IFCAT:
                    do_ifcat();
                    continue;
                case PrimitiveType::IFX:
                    do_ifx();
                    continue;
                case PrimitiveType::IFNUM:
                    do_ifnum();
                    continue;
                case PrimitiveType::IFDIM:
                    do_ifdim();
                    continue;
                case PrimitiveType::IFODD:
                    do_ifodd();
                    continue;
                case PrimitiveType::IFTRUE:
                    do_iftrue();
                    continue;
                case PrimitiveType::IFFALSE:
                    do_iffalse();
                    continue;
                case PrimitiveType::IFCASE:
                    do_ifcase();
                    continue;
                case PrimitiveType::IFDEFINED:
                    do_ifdefined();
                    continue;
                case PrimitiveType::ELSE:
                    do_else();
                    continue;
                case PrimitiveType::FI:
                    do_fi();
                    continue;
                case PrimitiveType::OR:
                    do_or();
                    continue;
                    
                // Mode tests - return false for now (no mode tracking)
                case PrimitiveType::IFVMODE:
                case PrimitiveType::IFHMODE:
                case PrimitiveType::IFMMODE:
                case PrimitiveType::IFINNER:
                case PrimitiveType::IFVOID:
                case PrimitiveType::IFHBOX:
                case PrimitiveType::IFVBOX:
                case PrimitiveType::IFEOF:
                    process_conditional(false);
                    continue;
                    
                // Definition commands - handle inline for convenience
                case PrimitiveType::DEF:
                    do_def(false, false);
                    continue;
                case PrimitiveType::EDEF:
                    do_def(false, true);
                    continue;
                case PrimitiveType::GDEF:
                    do_def(true, false);
                    continue;
                case PrimitiveType::XDEF:
                    do_def(true, true);
                    continue;
                case PrimitiveType::LET:
                    do_let(false);
                    continue;
                case PrimitiveType::FUTURELET:
                    do_futurelet();
                    continue;
                case PrimitiveType::GLOBAL:
                    // TODO: set global flag for next definition
                    continue;
                case PrimitiveType::LONG:
                    // TODO: set long flag for next definition
                    continue;
                case PrimitiveType::RELAX:
                    // Do nothing, consume the token
                    continue;
                case PrimitiveType::BEGINGROUP:
                case PrimitiveType::BGROUP:
                    begin_group();
                    continue;
                case PrimitiveType::ENDGROUP:
                case PrimitiveType::EGROUP:
                    end_group();
                    continue;
                case PrimitiveType::NEWCOUNT: {
                    // \newcount\foo - skip for now
                    Token cs = get_token();
                    (void)cs;
                    continue;
                }
                    
                default:
                    // Not an expandable primitive we handle
                    return t;
            }
        }
        
        if (entry->type == CommandEntry::Type::MACRO) {
            // Expand macro
            if (expansion_depth >= expansion_limit) {
                log_error("expander: expansion depth limit exceeded");
                return Token::make_end();
            }
            
            expansion_depth++;
            expand_macro(t, entry->macro);
            expansion_depth--;
            continue;
        }
        
        // Not expandable
        return t;
    }
}

TokenList Expander::expand_fully() {
    TokenList result(arena);
    
    while (!at_end()) {
        Token t = expand_token();
        if (t.is_end()) break;
        result.push_back(t);
    }
    
    return result;
}

// ============================================================================
// Macro Expansion
// ============================================================================

void Expander::expand_macro(const Token& cs, const MacroDef2* macro) {
    log_debug("expander: expanding \\%.*s", (int)macro->name_len, macro->name);
    
    // Parse arguments
    TokenList args[9];
    for (int i = 0; i < 9; i++) {
        args[i] = TokenList(arena);
    }
    
    if (!parse_macro_args(macro, args)) {
        log_error("expander: failed to parse arguments for \\%.*s", 
                  (int)macro->name_len, macro->name);
        return;
    }
    
    // Substitute parameters in replacement text
    TokenList result = macro->replacement.substitute(args, macro->param_count, arena);
    
    // Push result to input
    push_tokens(&result);
}

bool Expander::parse_macro_args(const MacroDef2* macro, TokenList* args) {
    // Simple case: no parameters
    if (macro->param_count == 0) {
        return true;
    }
    
    // Check if we have a parameter pattern
    if (macro->param_text.empty()) {
        // Simple undelimited parameters: each is a token or braced group
        for (int i = 0; i < macro->param_count; i++) {
            args[i] = read_argument();
        }
        return true;
    }
    
    // Delimited parameters - follow the parameter text pattern
    const TokenNode* pattern = macro->param_text.begin();
    
    while (pattern) {
        const Token& pt = pattern->token;
        
        if (pt.type == TokenType::PARAM) {
            int param_num = pt.param.num;
            if (param_num < 1 || param_num > 9) {
                pattern = pattern->next;
                continue;
            }
            
            // Check what comes after the parameter
            pattern = pattern->next;
            if (!pattern) {
                // Last parameter - read undelimited
                args[param_num - 1] = read_argument();
            } else if (pattern->token.type == TokenType::PARAM) {
                // Next is another parameter - read undelimited argument
                args[param_num - 1] = read_argument();
            } else {
                // Delimited by next tokens in pattern
                TokenList delimiter(arena);
                while (pattern && pattern->token.type != TokenType::PARAM) {
                    delimiter.push_back(pattern->token);
                    pattern = pattern->next;
                }
                args[param_num - 1] = read_delimited_argument(delimiter);
            }
        } else {
            // Match literal token
            Token t = get_token();
            // Should match pt - skip for now
            pattern = pattern->next;
        }
    }
    
    return true;
}

TokenList Expander::read_argument() {
    TokenList result(arena);
    
    // Skip spaces
    Token t = get_token();
    while (t.type == TokenType::CHAR && t.catcode == CatCode::SPACE) {
        t = get_token();
    }
    
    if (t.is_end()) {
        log_error("expander: unexpected end of input reading argument");
        return result;
    }
    
    if (t.type == TokenType::CHAR && t.catcode == CatCode::BEGIN_GROUP) {
        // Braced group - read until matching }
        return read_balanced_text();
    }
    
    // Single token
    result.push_back(t);
    return result;
}

TokenList Expander::read_delimited_argument(const TokenList& delimiter) {
    TokenList result(arena);
    int brace_depth = 0;
    
    // Read until we find the delimiter at brace level 0
    while (true) {
        Token t = get_token();
        if (t.is_end()) {
            log_error("expander: unexpected end reading delimited argument");
            break;
        }
        
        if (t.type == TokenType::CHAR) {
            if (t.catcode == CatCode::BEGIN_GROUP) {
                brace_depth++;
            } else if (t.catcode == CatCode::END_GROUP) {
                brace_depth--;
                if (brace_depth < 0) {
                    // Encountered a } that doesn't belong to us - push it back and return
                    log_error("expander: unbalanced braces in argument");
                    push_back(t);
                    return result;
                }
            }
        }
        
        // Check for delimiter match at level 0
        if (brace_depth == 0 && !delimiter.empty()) {
            bool match = true;
            TokenList saved(arena);
            saved.push_back(t);
            
            const TokenNode* dp = delimiter.begin();
            if (t.type == dp->token.type) {
                // Potential match - check rest
                dp = dp->next;
                while (dp && match) {
                    Token next = get_token();
                    saved.push_back(next);
                    // Simple comparison
                    if (next.type != dp->token.type) {
                        match = false;
                    } else if (next.type == TokenType::CHAR && 
                               next.chr.ch != dp->token.chr.ch) {
                        match = false;
                    } else if (next.type == TokenType::CS &&
                               (next.cs.len != dp->token.cs.len ||
                                memcmp(next.cs.name, dp->token.cs.name, next.cs.len) != 0)) {
                        match = false;
                    }
                    dp = dp->next;
                }
                
                if (match && !dp) {
                    // Full match - done
                    return result;
                }
            }
            
            // No match - push back saved tokens and add first to result
            while (!saved.empty()) {
                push_back(saved.pop_front());
            }
            t = get_token();  // Re-read first token
        }
        
        result.push_back(t);
    }
    
    return result;
}

TokenList Expander::read_balanced_text() {
    TokenList result(arena);
    int depth = 1;
    
    while (depth > 0) {
        Token t = get_token();
        if (t.is_end()) {
            log_error("expander: unexpected end in balanced text");
            break;
        }
        
        if (t.type == TokenType::CHAR) {
            if (t.catcode == CatCode::BEGIN_GROUP) {
                depth++;
            } else if (t.catcode == CatCode::END_GROUP) {
                depth--;
                if (depth == 0) {
                    break;  // Don't include final }
                }
            }
        }
        
        result.push_back(t);
    }
    
    return result;
}

// ============================================================================
// Definitions
// ============================================================================

void Expander::define_macro(const char* name, size_t name_len,
                           const TokenList& param_text, int param_count,
                           const TokenList& replacement,
                           bool is_global) {
    // Save old definition if we're in a group and not global
    if (!is_global && group_stack) {
        save_command(name, name_len);
    }
    
    MacroDef2* macro = (MacroDef2*)arena_alloc(arena, sizeof(MacroDef2));
    *macro = MacroDef2();
    
    // Copy name
    char* name_copy = (char*)arena_alloc(arena, name_len + 1);
    memcpy(name_copy, name, name_len);
    name_copy[name_len] = '\0';
    macro->name = name_copy;
    macro->name_len = (uint16_t)name_len;
    
    // Copy param text and replacement
    macro->param_text = param_text.copy(arena);
    macro->param_count = (int8_t)param_count;
    macro->replacement = replacement.copy(arena);
    macro->is_expandable = true;
    
    // Create entry
    CommandHashEntry entry;
    entry.name = name_copy;
    entry.name_len = name_len;
    entry.entry.type = CommandEntry::Type::MACRO;
    entry.entry.macro = macro;
    
    hashmap_set(commands, &entry);
    
    log_debug("expander: defined \\%.*s with %d params", (int)name_len, name, param_count);
}

void Expander::save_command(const char* name, size_t name_len) {
    if (!group_stack) return;
    
    // Check if we already saved this command in this group
    CommandHashEntry key;
    key.name = name;
    key.name_len = name_len;
    if (hashmap_get(group_stack->saved_commands, &key)) {
        return;  // Already saved, don't overwrite with current (changed) value
    }
    
    // Get current definition (if any)
    const CommandEntry* current = lookup(name, name_len);
    
    // Save current state
    char* name_copy = (char*)arena_alloc(arena, name_len + 1);
    memcpy(name_copy, name, name_len);
    name_copy[name_len] = '\0';
    
    CommandHashEntry save;
    save.name = name_copy;
    save.name_len = name_len;
    if (current) {
        save.entry = *current;
    } else {
        save.entry.type = CommandEntry::Type::UNDEFINED;
    }
    
    hashmap_set(group_stack->saved_commands, &save);
    log_debug("expander: saved \\%.*s for group restore", (int)name_len, name);
}

void Expander::let(const char* name, size_t name_len, const Token& target, bool is_global) {
    (void)is_global;  // TODO: implement global scope
    char* name_copy = (char*)arena_alloc(arena, name_len + 1);
    memcpy(name_copy, name, name_len);
    name_copy[name_len] = '\0';
    
    CommandHashEntry entry;
    entry.name = name_copy;
    entry.name_len = name_len;
    
    // Look up the meaning of the target
    if (target.type == TokenType::CS) {
        const CommandEntry* target_cmd = lookup(target);
        if (target_cmd) {
            // Copy the target's meaning
            entry.entry = *target_cmd;
        } else {
            // Target is undefined - make this undefined too
            entry.entry.type = CommandEntry::Type::UNDEFINED;
        }
    } else {
        // Target is a character token - store as LET
        entry.entry.type = CommandEntry::Type::LET;
        entry.entry.let_target = target;
    }
    
    hashmap_set(commands, &entry);
}

// ============================================================================
// Grouping
// ============================================================================

void Expander::begin_group() {
    GroupSave* save = (GroupSave*)arena_alloc(arena, sizeof(GroupSave));
    save->saved_commands = hashmap_new(sizeof(CommandHashEntry), 32, 0, 0,
                                       cmd_hash, cmd_compare, nullptr, nullptr);
    save->saved_counts = (int*)arena_alloc(arena, sizeof(count_regs));
    memcpy(save->saved_counts, count_regs, sizeof(count_regs));
    save->prev = group_stack;
    group_stack = save;
    log_debug("expander: begin_group, depth=%d", group_depth());
}

void Expander::end_group() {
    if (!group_stack) {
        log_error("expander: unbalanced grouping");
        return;
    }
    
    GroupSave* save = group_stack;
    group_stack = save->prev;
    log_debug("expander: end_group, depth=%d", group_depth());
    
    // Restore counts
    memcpy(count_regs, save->saved_counts, sizeof(count_regs));
    
    // Restore commands - iterate through saved entries and restore them
    size_t iter = 0;
    void* item;
    while (hashmap_iter(save->saved_commands, &iter, &item)) {
        CommandHashEntry* saved = (CommandHashEntry*)item;
        if (saved->entry.type == CommandEntry::Type::UNDEFINED) {
            // Was undefined before - delete current definition
            CommandHashEntry key;
            key.name = saved->name;
            key.name_len = saved->name_len;
            hashmap_delete(commands, &key);
        } else {
            // Restore old definition
            hashmap_set(commands, saved);
        }
    }
    
    hashmap_free(save->saved_commands);
}

int Expander::group_depth() const {
    int depth = 0;
    GroupSave* gs = group_stack;
    while (gs) {
        depth++;
        gs = gs->prev;
    }
    return depth;
}

// ============================================================================
// Registers
// ============================================================================

int Expander::get_count(int reg) const {
    return count_regs[reg & 255];
}

void Expander::set_count(int reg, int value, bool global) {
    count_regs[reg & 255] = value;
}

void Expander::advance_count(int reg, int by) {
    count_regs[reg & 255] += by;
}

// ============================================================================
// Expansion Primitives
// ============================================================================

void Expander::do_expandafter() {
    // \expandafter T1 T2 → expand T2 first, then put T1 back
    Token t1 = get_token();
    Token t2 = expand_token();
    push_back(t2);
    push_back(t1);
}

TokenList Expander::do_csname() {
    // \csname ... \endcsname → construct control sequence
    TokenList result(arena);
    char name_buf[256];
    size_t name_len = 0;
    
    while (true) {
        Token t = expand_token();
        if (t.is_end()) {
            log_error("expander: missing \\endcsname");
            break;
        }
        
        if (t.type == TokenType::CS && t.is_cs("endcsname")) {
            break;
        }
        
        // Add character to name
        if (t.type == TokenType::CHAR && name_len < sizeof(name_buf) - 1) {
            name_buf[name_len++] = t.chr.ch;
        }
    }
    
    // Create the control sequence token
    if (name_len > 0) {
        result.push_back(Token::make_cs(name_buf, name_len, arena));
    }
    
    return result;
}

TokenList Expander::do_string(const Token& t) {
    TokenList result(arena);
    
    if (t.type == TokenType::CS) {
        // \string\foo → \foo as character tokens
        result.push_back(Token::make_char('\\', CatCode::OTHER));
        for (size_t i = 0; i < t.cs.len; i++) {
            result.push_back(Token::make_char(t.cs.name[i], CatCode::OTHER));
        }
    } else if (t.type == TokenType::CHAR) {
        result.push_back(Token::make_char(t.chr.ch, CatCode::OTHER));
    }
    
    return result;
}

TokenList Expander::do_number(int n) {
    TokenList result(arena);
    
    char buf[32];
    int len = snprintf(buf, sizeof(buf), "%d", n);
    
    for (int i = 0; i < len; i++) {
        result.push_back(Token::make_char(buf[i], CatCode::OTHER));
    }
    
    return result;
}

TokenList Expander::do_romannumeral(int n) {
    TokenList result(arena);
    
    if (n <= 0) return result;
    
    // Standard roman numeral conversion
    struct { int value; const char* numeral; } table[] = {
        {1000, "m"}, {900, "cm"}, {500, "d"}, {400, "cd"},
        {100, "c"}, {90, "xc"}, {50, "l"}, {40, "xl"},
        {10, "x"}, {9, "ix"}, {5, "v"}, {4, "iv"}, {1, "i"}
    };
    
    for (auto& entry : table) {
        while (n >= entry.value) {
            for (const char* p = entry.numeral; *p; p++) {
                result.push_back(Token::make_char(*p, CatCode::OTHER));
            }
            n -= entry.value;
        }
    }
    
    return result;
}

TokenList Expander::do_meaning(const Token& t) {
    TokenList result(arena);
    const char* meaning = nullptr;
    
    const CommandEntry* entry = lookup(t);
    if (!entry || entry->type == CommandEntry::Type::UNDEFINED) {
        meaning = "undefined";
    } else if (entry->type == CommandEntry::Type::PRIMITIVE) {
        meaning = "primitive";
    } else if (entry->type == CommandEntry::Type::MACRO) {
        meaning = "macro";
    } else {
        meaning = "unknown";
    }
    
    for (const char* p = meaning; *p; p++) {
        result.push_back(Token::make_char(*p, CatCode::OTHER));
    }
    
    return result;
}

TokenList Expander::do_unexpanded() {
    // \unexpanded{...} → read braced text without expansion
    Token t = get_token();
    while (t.type == TokenType::CHAR && t.catcode == CatCode::SPACE) {
        t = get_token();
    }
    
    if (t.type != TokenType::CHAR || t.catcode != CatCode::BEGIN_GROUP) {
        log_error("expander: \\unexpanded requires braced argument");
        return TokenList(arena);
    }
    
    TokenList result = read_balanced_text();
    
    // Mark all tokens as noexpand
    for (TokenNode* node = result.begin(); node != nullptr; node = node->next) {
        node->token.noexpand = true;
    }
    
    return result;
}

int Expander::do_numexpr() {
    // Simplified numeric expression evaluation
    return scan_int();
}

// ============================================================================
// Conditionals
// ============================================================================

void Expander::process_conditional(bool result) {
    if (cond_depth >= MAX_COND_STACK) {
        log_error("expander: conditional nesting too deep");
        return;
    }
    
    cond_stack[cond_depth].type = CondState::Type::IF;
    cond_stack[cond_depth].result = result;
    cond_stack[cond_depth].else_seen = false;
    cond_depth++;
    
    if (!result) {
        // Skip to \else or \fi
        skip_conditional_branch(true);
    }
}

void Expander::skip_conditional_branch(bool skip_else) {
    int depth = 1;
    
    while (depth > 0) {
        Token t = get_token();
        if (t.is_end()) {
            log_error("expander: missing \\fi");
            return;
        }
        
        if (t.type != TokenType::CS) continue;
        
        const CommandEntry* entry = lookup(t);
        if (!entry || entry->type != CommandEntry::Type::PRIMITIVE) continue;
        
        PrimitiveType prim = entry->primitive;
        
        // Check for nested conditionals
        if (prim >= PrimitiveType::IF && prim <= PrimitiveType::IFCSNAME) {
            depth++;
        } else if (prim == PrimitiveType::FI) {
            depth--;
        } else if (prim == PrimitiveType::ELSE && depth == 1) {
            if (skip_else) {
                // Found \else at our level - continue from here
                return;
            }
        } else if (prim == PrimitiveType::OR && depth == 1) {
            // For \ifcase
            if (cond_depth > 0 && 
                cond_stack[cond_depth - 1].type == CondState::Type::IFCASE) {
                cond_stack[cond_depth - 1].or_count++;
                if (cond_stack[cond_depth - 1].or_count == 
                    cond_stack[cond_depth - 1].case_value) {
                    return;
                }
            }
        }
    }
    
    // Hit \fi - pop conditional
    if (cond_depth > 0) cond_depth--;
}

void Expander::do_if() {
    // \if<token1><token2> - compare character codes
    Token t1 = expand_token();
    Token t2 = expand_token();
    
    bool result = t1.char_code_equal(t2);
    process_conditional(result);
}

void Expander::do_ifcat() {
    // \ifcat<token1><token2> - compare category codes
    Token t1 = expand_token();
    Token t2 = expand_token();
    
    bool result = t1.catcode_equal(t2);
    process_conditional(result);
}

void Expander::do_ifx() {
    // \ifx<token1><token2> - compare meanings
    Token t1 = get_token();
    Token t2 = get_token();
    
    bool result = false;
    
    // Both control sequences?
    if (t1.type == TokenType::CS && t2.type == TokenType::CS) {
        if (t1.cs.len == t2.cs.len && 
            memcmp(t1.cs.name, t2.cs.name, t1.cs.len) == 0) {
            result = true;  // Same name
        } else {
            // Compare meanings
            const CommandEntry* e1 = lookup(t1);
            const CommandEntry* e2 = lookup(t2);
            
            if (!e1 && !e2) {
                result = true;  // Both undefined
            } else if (e1 && e2) {
                // Compare types
                if (e1->type == e2->type) {
                    if (e1->type == CommandEntry::Type::MACRO) {
                        // Compare macro definitions
                        // Simplified: just check if same macro object
                        result = (e1->macro == e2->macro);
                    } else if (e1->type == CommandEntry::Type::PRIMITIVE) {
                        result = (e1->primitive == e2->primitive);
                    }
                }
            }
        }
    } else if (t1.type == TokenType::CHAR && t2.type == TokenType::CHAR) {
        // Both characters - compare char and catcode
        result = (t1.chr.ch == t2.chr.ch && t1.catcode == t2.catcode);
    }
    
    process_conditional(result);
}

void Expander::do_ifnum() {
    // \ifnum<number><relation><number>
    int n1 = scan_int();
    
    // Get relation
    Token rel_tok = expand_token();
    char rel = '<';
    if (rel_tok.type == TokenType::CHAR) {
        rel = rel_tok.chr.ch;
    }
    
    int n2 = scan_int();
    
    bool result = false;
    switch (rel) {
        case '<': result = (n1 < n2); break;
        case '=': result = (n1 == n2); break;
        case '>': result = (n1 > n2); break;
    }
    
    process_conditional(result);
}

void Expander::do_ifdim() {
    // Simplified - just compare numbers
    float d1 = scan_dimen();
    
    Token rel_tok = expand_token();
    char rel = '<';
    if (rel_tok.type == TokenType::CHAR) {
        rel = rel_tok.chr.ch;
    }
    
    float d2 = scan_dimen();
    
    bool result = false;
    switch (rel) {
        case '<': result = (d1 < d2); break;
        case '=': result = (d1 == d2); break;
        case '>': result = (d1 > d2); break;
    }
    
    process_conditional(result);
}

void Expander::do_ifodd() {
    int n = scan_int();
    process_conditional((n % 2) != 0);
}

void Expander::do_iftrue() {
    process_conditional(true);
}

void Expander::do_iffalse() {
    process_conditional(false);
}

void Expander::do_ifcase() {
    int n = scan_int();
    
    if (cond_depth >= MAX_COND_STACK) {
        log_error("expander: conditional nesting too deep");
        return;
    }
    
    cond_stack[cond_depth].type = CondState::Type::IFCASE;
    cond_stack[cond_depth].case_value = n;
    cond_stack[cond_depth].or_count = 0;
    cond_stack[cond_depth].else_seen = false;
    cond_depth++;
    
    if (n != 0) {
        // Skip to the right \or
        skip_conditional_branch(true);
    }
}

void Expander::do_ifdefined() {
    Token t = get_token();
    bool result = is_defined(t);
    process_conditional(result);
}

void Expander::do_else() {
    if (cond_depth == 0) {
        log_error("expander: \\else without \\if");
        return;
    }
    
    // We're in the true branch - skip to \fi
    skip_conditional_branch(false);
}

void Expander::do_fi() {
    if (cond_depth == 0) {
        log_error("expander: \\fi without \\if");
        return;
    }
    cond_depth--;
}

void Expander::do_or() {
    if (cond_depth == 0) {
        log_error("expander: \\or without \\ifcase");
        return;
    }
    
    // We're in a matched case - skip rest
    skip_conditional_branch(false);
}

// ============================================================================
// Scanning
// ============================================================================

int Expander::scan_int() {
    // Skip spaces
    Token t = expand_token();
    while (t.type == TokenType::CHAR && t.catcode == CatCode::SPACE) {
        t = expand_token();
    }
    
    // Check for sign
    int sign = 1;
    while (t.type == TokenType::CHAR && (t.chr.ch == '+' || t.chr.ch == '-')) {
        if (t.chr.ch == '-') sign = -sign;
        t = expand_token();
    }
    
    // Check for special prefixes
    if (t.type == TokenType::CHAR && t.chr.ch == '`') {
        // `<char> → character code
        t = get_token();
        if (t.type == TokenType::CS) {
            return sign * (unsigned char)t.cs.name[0];
        } else if (t.type == TokenType::CHAR) {
            return sign * (unsigned char)t.chr.ch;
        }
        return 0;
    }
    
    // Check for hex/octal
    if (t.type == TokenType::CHAR && t.chr.ch == '"') {
        // Hex
        int value = 0;
        while (true) {
            t = expand_token();
            if (t.type != TokenType::CHAR) {
                // Don't push back END_OF_INPUT
                if (t.type != TokenType::END_OF_INPUT) {
                    push_back(t);
                }
                break;
            }
            char c = t.chr.ch;
            if (c >= '0' && c <= '9') {
                value = value * 16 + (c - '0');
            } else if (c >= 'A' && c <= 'F') {
                value = value * 16 + (c - 'A' + 10);
            } else if (c >= 'a' && c <= 'f') {
                value = value * 16 + (c - 'a' + 10);
            } else if (t.catcode == CatCode::SPACE) {
                // TeX consumes one trailing space after a number
                break;
            } else {
                push_back(t);
                break;
            }
        }
        return sign * value;
    }
    
    if (t.type == TokenType::CHAR && t.chr.ch == '\'') {
        // Octal
        int value = 0;
        while (true) {
            t = expand_token();
            if (t.type != TokenType::CHAR) {
                // Don't push back END_OF_INPUT
                if (t.type != TokenType::END_OF_INPUT) {
                    push_back(t);
                }
                break;
            }
            char c = t.chr.ch;
            if (c >= '0' && c <= '7') {
                value = value * 8 + (c - '0');
            } else if (t.catcode == CatCode::SPACE) {
                // TeX consumes one trailing space after a number
                break;
            } else {
                push_back(t);
                break;
            }
        }
        return sign * value;
    }
    
    // Decimal
    if (t.type == TokenType::CHAR && t.chr.ch >= '0' && t.chr.ch <= '9') {
        int value = t.chr.ch - '0';
        while (true) {
            t = expand_token();
            if (t.type != TokenType::CHAR) {
                // Don't push back END_OF_INPUT
                if (t.type != TokenType::END_OF_INPUT) {
                    push_back(t);
                }
                break;
            }
            char c = t.chr.ch;
            if (c >= '0' && c <= '9') {
                value = value * 10 + (c - '0');
            } else if (t.catcode == CatCode::SPACE) {
                // TeX consumes one trailing space after a number
                break;
            } else {
                push_back(t);
                break;
            }
        }
        return sign * value;
    }
    
    // Check for count register
    if (t.type == TokenType::CS) {
        const CommandEntry* entry = lookup(t);
        if (entry && entry->type == CommandEntry::Type::PRIMITIVE) {
            if (entry->primitive == PrimitiveType::COUNT) {
                int reg = scan_register_num();
                return sign * count_regs[reg & 255];
            }
        }
    }
    
    push_back(t);
    return 0;
}

float Expander::scan_dimen() {
    // Simplified - just scan a number and optional unit
    float value = (float)scan_int();
    
    // Skip optional spaces
    Token t = expand_token();
    while (t.type == TokenType::CHAR && t.catcode == CatCode::SPACE) {
        t = expand_token();
    }
    
    // Check for unit
    // Simplified - just push back
    push_back(t);
    
    return value;
}

bool Expander::scan_keyword(const char* keyword) {
    const char* p = keyword;
    TokenList saved(arena);
    
    while (*p) {
        Token t = expand_token();
        saved.push_back(t);
        
        if (t.type != TokenType::CHAR) {
            // No match - push back
            while (!saved.empty()) {
                push_back(saved.pop_front());
            }
            return false;
        }
        
        char c = t.chr.ch;
        if (tolower(c) != tolower(*p)) {
            // No match
            while (!saved.empty()) {
                push_back(saved.pop_front());
            }
            return false;
        }
        p++;
    }
    
    return true;
}

int Expander::scan_register_num() {
    return scan_int() & 255;
}

// ============================================================================
// Primitive Execution
// ============================================================================

void Expander::execute_primitive(PrimitiveType type, const Token& cs) {
    switch (type) {
        case PrimitiveType::DEF:
            do_def(false, false);
            break;
        case PrimitiveType::EDEF:
            do_def(false, true);
            break;
        case PrimitiveType::GDEF:
            do_def(true, false);
            break;
        case PrimitiveType::XDEF:
            do_def(true, true);
            break;
        case PrimitiveType::LET:
            do_let(false);
            break;
        case PrimitiveType::FUTURELET:
            do_futurelet();
            break;
        case PrimitiveType::BEGINGROUP:
        case PrimitiveType::BGROUP:
            begin_group();
            break;
        case PrimitiveType::ENDGROUP:
        case PrimitiveType::EGROUP:
            end_group();
            break;
        case PrimitiveType::RELAX:
            // Do nothing
            break;
        default:
            log_debug("expander: unhandled primitive %d", (int)type);
            break;
    }
}

void Expander::do_def(bool is_global, bool is_edef) {
    // \def\cs<param text>{<replacement text>}
    Token cs = get_token();
    if (cs.type != TokenType::CS) {
        log_error("expander: \\def requires control sequence");
        return;
    }
    
    // Read parameter text until {
    TokenList param_text(arena);
    int param_count = 0;
    
    while (true) {
        Token t = get_token();
        if (t.is_end()) {
            log_error("expander: unexpected end in \\def");
            return;
        }
        
        if (t.type == TokenType::CHAR && t.catcode == CatCode::BEGIN_GROUP) {
            break;
        }
        
        if (t.type == TokenType::PARAM) {
            if (t.param.num > param_count + 1) {
                log_error("expander: parameters must be in order");
                return;
            }
            param_count = t.param.num;
        }
        
        param_text.push_back(t);
    }
    
    // Read replacement text
    TokenList replacement(arena);
    if (is_edef) {
        // \edef - expand replacement text
        int depth = 1;
        while (depth > 0) {
            Token t = expand_token();
            if (t.is_end()) {
                log_error("expander: unexpected end in \\edef replacement");
                break;
            }
            
            if (t.type == TokenType::CHAR) {
                if (t.catcode == CatCode::BEGIN_GROUP) depth++;
                else if (t.catcode == CatCode::END_GROUP) {
                    depth--;
                    if (depth == 0) break;
                }
            }
            replacement.push_back(t);
        }
    } else {
        // \def - don't expand
        replacement = read_balanced_text();
    }
    
    define_macro(cs.cs.name, cs.cs.len, param_text, param_count, replacement, is_global);
}

void Expander::do_let(bool is_global) {
    // \let\cs1=\cs2 or \let\cs1\cs2
    Token cs1 = get_token();
    if (cs1.type != TokenType::CS) {
        log_error("expander: \\let requires control sequence");
        return;
    }
    
    // Skip optional =
    Token t = get_token();
    if (t.type == TokenType::CHAR && t.chr.ch == '=') {
        t = get_token();
    }
    
    // Skip optional space after =
    while (t.type == TokenType::CHAR && t.catcode == CatCode::SPACE) {
        t = get_token();
    }
    
    let(cs1.cs.name, cs1.cs.len, t, is_global);
}

void Expander::do_futurelet() {
    // \futurelet\cs<token1><token2>
    Token cs = get_token();
    Token t1 = get_token();
    Token t2 = get_token();
    
    // Let \cs = t2
    if (cs.type == TokenType::CS) {
        let(cs.cs.name, cs.cs.len, t2, false);
    }
    
    // Put t1 and t2 back
    push_back(t2);
    push_back(t1);
}

} // namespace tex
