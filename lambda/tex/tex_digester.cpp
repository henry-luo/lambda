// tex_digester.cpp - TeX Digester (Stomach) Implementation
//
// Processes expanded tokens and builds the semantic IR.
//
// Reference: TeXBook Chapter 24, Latex_Typeset_Design3.md Section 3

#include "tex_digester.hpp"
#include "tex_digested.hpp"
#include "tex_expander.hpp"
#include "tex_token.hpp"
#include "../../lib/log.h"
#include "../../lib/strbuf.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>

namespace tex {

// ============================================================================
// Digested Type Names
// ============================================================================

const char* digested_type_name(DigestedType type) {
    switch (type) {
        case DigestedType::BOX:     return "BOX";
        case DigestedType::LIST:    return "LIST";
        case DigestedType::WHATSIT: return "WHATSIT";
        case DigestedType::GLUE:    return "GLUE";
        case DigestedType::KERN:    return "KERN";
        case DigestedType::PENALTY: return "PENALTY";
        case DigestedType::RULE:    return "RULE";
        case DigestedType::MARK:    return "MARK";
        case DigestedType::INSERT:  return "INSERT";
        case DigestedType::SPECIAL: return "SPECIAL";
        case DigestedType::MATH:    return "MATH";
        case DigestedType::CHAR:    return "CHAR";
        case DigestedType::DISC:    return "DISC";
        default:                    return "UNKNOWN";
    }
}

const char* mode_name(DigesterMode mode) {
    switch (mode) {
        case DigesterMode::VERTICAL:            return "vertical";
        case DigesterMode::INTERNAL_VERTICAL:   return "internal vertical";
        case DigesterMode::HORIZONTAL:          return "horizontal";
        case DigesterMode::RESTRICTED_HORIZONTAL: return "restricted horizontal";
        case DigesterMode::MATH:                return "display math";
        case DigesterMode::INLINE_MATH:         return "inline math";
        default:                                return "unknown";
    }
}

// ============================================================================
// PropertyMap Implementation (simple linked list)
// ============================================================================

void PropertyMap::set(const char* key, const char* value) {
    // check if key exists and update
    for (PropertyEntry* e = head; e; e = e->next) {
        if (strcmp(e->key, key) == 0) {
            size_t val_len = strlen(value);
            char* val_copy = (char*)arena_alloc(arena, val_len + 1);
            memcpy(val_copy, value, val_len + 1);
            e->value = val_copy;
            return;
        }
    }
    
    // create new entry
    PropertyEntry* entry = (PropertyEntry*)arena_alloc(arena, sizeof(PropertyEntry));
    
    size_t key_len = strlen(key);
    size_t val_len = strlen(value);
    
    char* key_copy = (char*)arena_alloc(arena, key_len + 1);
    char* val_copy = (char*)arena_alloc(arena, val_len + 1);
    
    memcpy(key_copy, key, key_len + 1);
    memcpy(val_copy, value, val_len + 1);
    
    entry->key = key_copy;
    entry->value = val_copy;
    entry->next = head;
    head = entry;
}

const char* PropertyMap::get(const char* key) const {
    for (PropertyEntry* e = head; e; e = e->next) {
        if (strcmp(e->key, key) == 0) {
            return e->value;
        }
    }
    return nullptr;
}

bool PropertyMap::has(const char* key) const {
    return get(key) != nullptr;
}

// ============================================================================
// GlueSpec Common Values
// ============================================================================

GlueSpec GlueSpec::parfillskip() {
    GlueSpec g;
    g.space = 0;
    g.stretch = 1.0f;
    g.stretch_order = GlueOrder::Fill;
    return g;
}

GlueSpec GlueSpec::parskip() {
    GlueSpec g;
    g.space = 0;
    g.stretch = 1.0f;
    g.stretch_order = GlueOrder::Normal;
    return g;
}

GlueSpec GlueSpec::baselineskip() {
    GlueSpec g;
    g.space = 12.0f;
    return g;
}

GlueSpec GlueSpec::lineskip() {
    GlueSpec g;
    g.space = 1.0f;
    return g;
}

GlueSpec GlueSpec::topskip() {
    GlueSpec g;
    g.space = 10.0f;
    return g;
}

GlueSpec GlueSpec::abovedisplayskip() {
    GlueSpec g;
    g.space = 12.0f;
    g.stretch = 3.0f;
    g.shrink = 9.0f;
    return g;
}

GlueSpec GlueSpec::belowdisplayskip() {
    return abovedisplayskip();
}

// ============================================================================
// DigestedNode Factory Methods
// ============================================================================

DigestedNode* DigestedNode::make_box(Arena* arena, const char* text, size_t len,
                                      const DigestedFontSpec& font) {
    DigestedNode* node = (DigestedNode*)arena_alloc(arena, sizeof(DigestedNode));
    memset(node, 0, sizeof(DigestedNode));
    
    node->type = DigestedType::BOX;
    node->font = font;
    
    char* text_copy = (char*)arena_alloc(arena, len + 1);
    memcpy(text_copy, text, len);
    text_copy[len] = '\0';
    
    node->content.box.text = text_copy;
    node->content.box.len = len;
    node->content.box.width = -1;
    node->content.box.height = -1;
    node->content.box.depth = -1;
    
    return node;
}

DigestedNode* DigestedNode::make_char(Arena* arena, int32_t codepoint,
                                       const DigestedFontSpec& font) {
    DigestedNode* node = (DigestedNode*)arena_alloc(arena, sizeof(DigestedNode));
    memset(node, 0, sizeof(DigestedNode));
    
    node->type = DigestedType::CHAR;
    node->font = font;
    
    node->content.chr.codepoint = codepoint;
    node->content.chr.width = -1;
    node->content.chr.height = -1;
    node->content.chr.depth = -1;
    
    return node;
}

DigestedNode* DigestedNode::make_list(Arena* arena, bool is_horizontal) {
    DigestedNode* node = (DigestedNode*)arena_alloc(arena, sizeof(DigestedNode));
    memset(node, 0, sizeof(DigestedNode));
    
    node->type = DigestedType::LIST;
    node->content.list.head = nullptr;
    node->content.list.tail = nullptr;
    node->content.list.count = 0;
    node->content.list.is_horizontal = is_horizontal;
    
    if (is_horizontal) {
        node->flags |= FLAG_HORIZONTAL;
    } else {
        node->flags |= FLAG_VERTICAL;
    }
    
    return node;
}

DigestedNode* DigestedNode::make_whatsit(Arena* arena, const char* name, size_t name_len) {
    DigestedNode* node = (DigestedNode*)arena_alloc(arena, sizeof(DigestedNode));
    memset(node, 0, sizeof(DigestedNode));
    
    node->type = DigestedType::WHATSIT;
    
    char* name_copy = (char*)arena_alloc(arena, name_len + 1);
    memcpy(name_copy, name, name_len);
    name_copy[name_len] = '\0';
    
    node->content.whatsit.name = name_copy;
    node->content.whatsit.name_len = name_len;
    node->content.whatsit.definition = nullptr;
    node->content.whatsit.args = nullptr;
    node->content.whatsit.arg_count = 0;
    
    // allocate property map
    PropertyMap* props = (PropertyMap*)arena_alloc(arena, sizeof(PropertyMap));
    props->init(arena);
    node->content.whatsit.properties = props;
    
    return node;
}

DigestedNode* DigestedNode::make_glue(Arena* arena, const GlueSpec& spec) {
    DigestedNode* node = (DigestedNode*)arena_alloc(arena, sizeof(DigestedNode));
    memset(node, 0, sizeof(DigestedNode));
    
    node->type = DigestedType::GLUE;
    node->content.glue = spec;
    
    return node;
}

DigestedNode* DigestedNode::make_kern(Arena* arena, float amount) {
    DigestedNode* node = (DigestedNode*)arena_alloc(arena, sizeof(DigestedNode));
    memset(node, 0, sizeof(DigestedNode));
    
    node->type = DigestedType::KERN;
    node->content.kern.amount = amount;
    
    return node;
}

DigestedNode* DigestedNode::make_penalty(Arena* arena, int value) {
    DigestedNode* node = (DigestedNode*)arena_alloc(arena, sizeof(DigestedNode));
    memset(node, 0, sizeof(DigestedNode));
    
    node->type = DigestedType::PENALTY;
    node->content.penalty.value = value;
    
    return node;
}

DigestedNode* DigestedNode::make_rule(Arena* arena, float width, float height, float depth) {
    DigestedNode* node = (DigestedNode*)arena_alloc(arena, sizeof(DigestedNode));
    memset(node, 0, sizeof(DigestedNode));
    
    node->type = DigestedType::RULE;
    node->content.rule.width = width;
    node->content.rule.height = height;
    node->content.rule.depth = depth;
    
    return node;
}

DigestedNode* DigestedNode::make_mark(Arena* arena, const char* text, size_t len, int mark_class) {
    DigestedNode* node = (DigestedNode*)arena_alloc(arena, sizeof(DigestedNode));
    memset(node, 0, sizeof(DigestedNode));
    
    node->type = DigestedType::MARK;
    
    char* text_copy = (char*)arena_alloc(arena, len + 1);
    memcpy(text_copy, text, len);
    text_copy[len] = '\0';
    
    node->content.mark.text = text_copy;
    node->content.mark.len = len;
    node->content.mark.mark_class = mark_class;
    
    return node;
}

DigestedNode* DigestedNode::make_insert(Arena* arena, int insert_class, DigestedNode* content) {
    DigestedNode* node = (DigestedNode*)arena_alloc(arena, sizeof(DigestedNode));
    memset(node, 0, sizeof(DigestedNode));
    
    node->type = DigestedType::INSERT;
    node->content.insert.insert_class = insert_class;
    node->content.insert.content = content;
    node->content.insert.natural_height = 0;
    node->content.insert.split_max = 1000000.0f;
    
    return node;
}

DigestedNode* DigestedNode::make_special(Arena* arena, const char* command, size_t len) {
    DigestedNode* node = (DigestedNode*)arena_alloc(arena, sizeof(DigestedNode));
    memset(node, 0, sizeof(DigestedNode));
    
    node->type = DigestedType::SPECIAL;
    
    char* cmd_copy = (char*)arena_alloc(arena, len + 1);
    memcpy(cmd_copy, command, len);
    cmd_copy[len] = '\0';
    
    node->content.special.command = cmd_copy;
    node->content.special.len = len;
    
    return node;
}

DigestedNode* DigestedNode::make_math(Arena* arena, DigestedNode* content, bool display) {
    DigestedNode* node = (DigestedNode*)arena_alloc(arena, sizeof(DigestedNode));
    memset(node, 0, sizeof(DigestedNode));
    
    node->type = DigestedType::MATH;
    node->content.math.content = content;
    node->content.math.display = display;
    node->flags |= FLAG_MATH;
    
    return node;
}

DigestedNode* DigestedNode::make_disc(Arena* arena, DigestedNode* pre, 
                                       DigestedNode* post, DigestedNode* nobreak) {
    DigestedNode* node = (DigestedNode*)arena_alloc(arena, sizeof(DigestedNode));
    memset(node, 0, sizeof(DigestedNode));
    
    node->type = DigestedType::DISC;
    node->content.disc.pre = pre;
    node->content.disc.post = post;
    node->content.disc.nobreak = nobreak;
    
    return node;
}

// ============================================================================
// DigestedNode List Operations
// ============================================================================

void DigestedNode::append(DigestedNode* node) {
    if (type != DigestedType::LIST) {
        log_error("digester: append called on non-list node");
        return;
    }
    
    node->prev = content.list.tail;
    node->next = nullptr;
    
    if (content.list.tail) {
        content.list.tail->next = node;
    } else {
        content.list.head = node;
    }
    content.list.tail = node;
    content.list.count++;
}

void DigestedNode::prepend(DigestedNode* node) {
    if (type != DigestedType::LIST) {
        log_error("digester: prepend called on non-list node");
        return;
    }
    
    node->next = content.list.head;
    node->prev = nullptr;
    
    if (content.list.head) {
        content.list.head->prev = node;
    } else {
        content.list.tail = node;
    }
    content.list.head = node;
    content.list.count++;
}

size_t DigestedNode::list_length() const {
    if (type != DigestedType::LIST) return 0;
    return content.list.count;
}

// ============================================================================
// DigestedNode Whatsit Operations
// ============================================================================

void DigestedNode::set_property(const char* key, const char* value) {
    if (type != DigestedType::WHATSIT || !content.whatsit.properties) {
        log_error("digester: set_property called on non-whatsit node");
        return;
    }
    content.whatsit.properties->set(key, value);
}

const char* DigestedNode::get_property(const char* key) const {
    if (type != DigestedType::WHATSIT || !content.whatsit.properties) {
        return nullptr;
    }
    return content.whatsit.properties->get(key);
}

// ============================================================================
// CommandRegistry Implementation
// ============================================================================

CommandRegistry::CommandRegistry(Arena* a) 
    : arena(a)
    , command_list(nullptr)
    , save_stack(nullptr)
    , group_depth(0) 
{
}

CommandRegistry::~CommandRegistry() {
    // arena handles cleanup
}

void CommandRegistry::define_macro(const char* name, const char* params, 
                                    const char* replacement) {
    size_t name_len = strlen(name);
    
    CommandDef* def = (CommandDef*)arena_alloc(arena, sizeof(CommandDef));
    new (def) CommandDef();
    
    char* name_copy = (char*)arena_alloc(arena, name_len + 1);
    memcpy(name_copy, name, name_len + 1);
    def->name = name_copy;
    def->name_len = name_len;
    def->type = CommandType::MACRO;
    def->params = params;
    def->param_count = params ? (int)strlen(params) / 2 : 0;
    
    if (replacement) {
        size_t rep_len = strlen(replacement);
        char* rep_copy = (char*)arena_alloc(arena, rep_len + 1);
        memcpy(rep_copy, replacement, rep_len + 1);
        def->replacement = rep_copy;
        def->replacement_len = rep_len;
    }
    
    CommandEntry* entry = (CommandEntry*)arena_alloc(arena, sizeof(CommandEntry));
    entry->def = *def;
    entry->next = command_list;
    command_list = entry;
}

void CommandRegistry::define_primitive(const char* name, const char* params, 
                                         PrimitiveFn fn) {
    size_t name_len = strlen(name);
    
    CommandDef* def = (CommandDef*)arena_alloc(arena, sizeof(CommandDef));
    new (def) CommandDef();
    
    char* name_copy = (char*)arena_alloc(arena, name_len + 1);
    memcpy(name_copy, name, name_len + 1);
    def->name = name_copy;
    def->name_len = name_len;
    def->type = CommandType::PRIMITIVE;
    def->params = params;
    def->callback = (ConstructorFn)fn;
    def->use_callback = true;
    
    CommandEntry* entry = (CommandEntry*)arena_alloc(arena, sizeof(CommandEntry));
    entry->def = *def;
    entry->next = command_list;
    command_list = entry;
}

void CommandRegistry::define_constructor(const char* name, const char* params, 
                                          const char* pattern) {
    size_t name_len = strlen(name);
    
    CommandDef* def = (CommandDef*)arena_alloc(arena, sizeof(CommandDef));
    new (def) CommandDef();
    
    char* name_copy = (char*)arena_alloc(arena, name_len + 1);
    memcpy(name_copy, name, name_len + 1);
    def->name = name_copy;
    def->name_len = name_len;
    def->type = CommandType::CONSTRUCTOR;
    def->params = params;
    def->pattern = pattern;
    def->use_callback = false;
    
    int count = 0;
    if (params) {
        for (const char* p = params; *p; p++) {
            if (*p == '{') count++;
        }
    }
    def->param_count = count;
    
    CommandEntry* entry = (CommandEntry*)arena_alloc(arena, sizeof(CommandEntry));
    entry->def = *def;
    entry->next = command_list;
    command_list = entry;
}

void CommandRegistry::define_constructor_fn(const char* name, const char* params, 
                                             ConstructorFn fn) {
    size_t name_len = strlen(name);
    
    CommandDef* def = (CommandDef*)arena_alloc(arena, sizeof(CommandDef));
    new (def) CommandDef();
    
    char* name_copy = (char*)arena_alloc(arena, name_len + 1);
    memcpy(name_copy, name, name_len + 1);
    def->name = name_copy;
    def->name_len = name_len;
    def->type = CommandType::CONSTRUCTOR;
    def->params = params;
    def->callback = fn;
    def->use_callback = true;
    
    int count = 0;
    if (params) {
        for (const char* p = params; *p; p++) {
            if (*p == '{') count++;
        }
    }
    def->param_count = count;
    
    CommandEntry* entry = (CommandEntry*)arena_alloc(arena, sizeof(CommandEntry));
    entry->def = *def;
    entry->next = command_list;
    command_list = entry;
}

void CommandRegistry::define_environment(const char* name, const char* begin_pattern, 
                                          const char* end_pattern) {
    size_t name_len = strlen(name);
    
    // create begin@name command
    StrBuf* sb = strbuf_new();
    strbuf_append_str(sb, "begin@");
    strbuf_append_str_n(sb, name, name_len);
    
    char* begin_name = (char*)arena_alloc(arena, sb->length + 1);
    memcpy(begin_name, sb->str, sb->length + 1);
    
    CommandDef* begin_def = (CommandDef*)arena_alloc(arena, sizeof(CommandDef));
    new (begin_def) CommandDef();
    begin_def->name = begin_name;
    begin_def->name_len = sb->length;
    begin_def->type = CommandType::ENVIRONMENT;
    begin_def->pattern = begin_pattern;
    
    CommandEntry* begin_entry = (CommandEntry*)arena_alloc(arena, sizeof(CommandEntry));
    begin_entry->def = *begin_def;
    begin_entry->next = command_list;
    command_list = begin_entry;
    
    // create end@name command
    strbuf_reset(sb);
    strbuf_append_str(sb, "end@");
    strbuf_append_str_n(sb, name, name_len);
    
    char* end_name = (char*)arena_alloc(arena, sb->length + 1);
    memcpy(end_name, sb->str, sb->length + 1);
    
    CommandDef* end_def = (CommandDef*)arena_alloc(arena, sizeof(CommandDef));
    new (end_def) CommandDef();
    end_def->name = end_name;
    end_def->name_len = sb->length;
    end_def->type = CommandType::ENVIRONMENT;
    end_def->pattern = end_pattern;
    
    CommandEntry* end_entry = (CommandEntry*)arena_alloc(arena, sizeof(CommandEntry));
    end_entry->def = *end_def;
    end_entry->next = command_list;
    command_list = end_entry;
    
    strbuf_free(sb);
}

void CommandRegistry::define_math(const char* name, const char* meaning, const char* role) {
    size_t name_len = strlen(name);
    
    CommandDef* def = (CommandDef*)arena_alloc(arena, sizeof(CommandDef));
    new (def) CommandDef();
    
    char* name_copy = (char*)arena_alloc(arena, name_len + 1);
    memcpy(name_copy, name, name_len + 1);
    def->name = name_copy;
    def->name_len = name_len;
    def->type = CommandType::MATH;
    def->is_math = true;
    def->replacement = meaning;
    def->pattern = role;
    
    CommandEntry* entry = (CommandEntry*)arena_alloc(arena, sizeof(CommandEntry));
    entry->def = *def;
    entry->next = command_list;
    command_list = entry;
}

const CommandDef* CommandRegistry::lookup(const char* name, size_t len) const {
    for (CommandEntry* e = command_list; e; e = e->next) {
        if (e->def.name_len == len && memcmp(e->def.name, name, len) == 0) {
            return &e->def;
        }
    }
    return nullptr;
}

const CommandDef* CommandRegistry::lookup(const char* name) const {
    return lookup(name, strlen(name));
}

bool CommandRegistry::is_defined(const char* name, size_t len) const {
    return lookup(name, len) != nullptr;
}

void CommandRegistry::begin_group() {
    group_depth++;
}

void CommandRegistry::end_group() {
    // restore saved definitions (simplified - just decrement depth)
    if (group_depth > 0) group_depth--;
}

void CommandRegistry::make_global(const char* name) {
    // TODO: implement
}

// ============================================================================
// Digester Implementation
// ============================================================================

Digester::Digester(Expander* exp, Arena* a)
    : expander(exp)
    , arena(a)
    , registry(nullptr)
    , current_mode(DigesterMode::VERTICAL)
    , prev_mode(DigesterMode::VERTICAL)
    , list_stack(nullptr)
    , current_list(nullptr)
    , group_level(0)
    , group_stack(nullptr)
    , counter_list(nullptr)
    , label_list(nullptr)
    , pending_label(nullptr)
    , footnotes(nullptr)
    , footnote_count(0)
    , footnote_capacity(0)
{
    strbuf = strbuf_new();
    font = DigestedFontSpec::roman(10.0f);
    
    // create root document list (vertical)
    current_list = DigestedNode::make_list(arena, false);
    push_list(current_list);
}

Digester::~Digester() {
    strbuf_free(strbuf);
}

// ============================================================================
// Main Digestion Interface
// ============================================================================

DigestedNode* Digester::digest() {
    log_debug("digester: starting digestion in %s mode", mode_name(current_mode));
    
    while (!expander->at_end()) {
        Token token = expander->expand_token();
        if (token.is_end()) break;
        
        digest_token(token);
    }
    
    // close any open paragraph
    if (is_horizontal()) {
        end_paragraph();
    }
    
    log_debug("digester: digestion complete, %zu top-level nodes", 
              current_list ? current_list->list_length() : 0);
    
    return current_list;
}

void Digester::digest_token(const Token& token) {
    switch (token.type) {
        case TokenType::CHAR:
            process_character(token);
            break;
            
        case TokenType::CS:
        case TokenType::CS_ACTIVE:
            process_control_sequence(token);
            break;
            
        case TokenType::PARAM:
            error("unexpected parameter token");
            break;
            
        case TokenType::END_OF_INPUT:
            break;
    }
}

DigestedNode* Digester::digest_until(const char* end_cs) {
    size_t end_len = strlen(end_cs);
    DigestedNode* list = DigestedNode::make_list(arena, is_horizontal());
    
    push_list(list);
    
    while (!expander->at_end()) {
        Token token = expander->expand_token();
        if (token.is_end()) break;
        
        if (token.is_cs() && token.cs.len == end_len &&
            memcmp(token.cs.name, end_cs, end_len) == 0) {
            break;
        }
        
        digest_token(token);
    }
    
    pop_list();
    return list;
}

DigestedNode* Digester::digest_group() {
    DigestedNode* list = DigestedNode::make_list(arena, is_horizontal());
    
    begin_group();
    push_list(list);
    
    int depth = 1;
    while (!expander->at_end() && depth > 0) {
        Token token = expander->expand_token();
        if (token.is_end()) break;
        
        if (token.has_catcode(CatCode::BEGIN_GROUP)) {
            depth++;
            begin_group();
        } else if (token.has_catcode(CatCode::END_GROUP)) {
            depth--;
            if (depth > 0) {
                end_group();
            }
        } else {
            digest_token(token);
        }
    }
    
    pop_list();
    end_group();
    
    return list;
}

// ============================================================================
// Mode Management
// ============================================================================

void Digester::set_mode(DigesterMode m) {
    prev_mode = current_mode;
    current_mode = m;
    log_debug("digester: mode change %s -> %s", mode_name(prev_mode), mode_name(m));
}

bool Digester::is_horizontal() const {
    return current_mode == DigesterMode::HORIZONTAL || 
           current_mode == DigesterMode::RESTRICTED_HORIZONTAL;
}

bool Digester::is_vertical() const {
    return current_mode == DigesterMode::VERTICAL || 
           current_mode == DigesterMode::INTERNAL_VERTICAL;
}

bool Digester::is_math() const {
    return current_mode == DigesterMode::MATH || 
           current_mode == DigesterMode::INLINE_MATH;
}

void Digester::begin_paragraph() {
    if (is_horizontal()) return;
    
    log_debug("digester: begin paragraph");
    
    if (current_list && current_list->list_length() > 0) {
        add_glue(GlueSpec::parskip());
    }
    
    DigestedNode* para = DigestedNode::make_list(arena, true);
    push_list(para);
    set_mode(DigesterMode::HORIZONTAL);
}

void Digester::end_paragraph() {
    if (!is_horizontal()) return;
    
    log_debug("digester: end paragraph");
    
    add_glue(GlueSpec::parfillskip());
    
    DigestedNode* para = pop_list();
    set_mode(DigesterMode::VERTICAL);
    
    if (para->list_length() > 0) {
        add_node(para);
    }
}

void Digester::begin_math(bool display) {
    if (is_math()) {
        error("already in math mode");
        return;
    }
    
    log_debug("digester: begin %s math", display ? "display" : "inline");
    
    if (display) {
        if (is_horizontal()) {
            end_paragraph();
        }
        add_glue(GlueSpec::abovedisplayskip());
        set_mode(DigesterMode::MATH);
    } else {
        if (!is_horizontal()) {
            begin_paragraph();
        }
        set_mode(DigesterMode::INLINE_MATH);
    }
    
    DigestedNode* math_list = DigestedNode::make_list(arena, true);
    math_list->flags |= DigestedNode::FLAG_MATH;
    push_list(math_list);
}

void Digester::end_math() {
    if (!is_math()) {
        error("not in math mode");
        return;
    }
    
    bool display = (current_mode == DigesterMode::MATH);
    log_debug("digester: end %s math", display ? "display" : "inline");
    
    DigestedNode* math_list = pop_list();
    DigestedNode* math_node = DigestedNode::make_math(arena, math_list, display);
    
    if (display) {
        set_mode(DigesterMode::VERTICAL);
        add_node(math_node);
        add_glue(GlueSpec::belowdisplayskip());
    } else {
        set_mode(DigesterMode::HORIZONTAL);
        add_node(math_node);
    }
}

// ============================================================================
// Font State
// ============================================================================

void Digester::set_font(const DigestedFontSpec& f) {
    font = f;
}

void Digester::set_font_family(const char* family) {
    font.family = family;
}

void Digester::set_font_size(float size_pt) {
    font.size_pt = size_pt;
}

void Digester::set_font_style(DigestedFontSpec::Flags flag, bool on) {
    if (on) {
        font.set(flag);
    } else {
        font.clear(flag);
    }
}

// ============================================================================
// Counter Management
// ============================================================================

Counter* Digester::get_counter(const char* name) {
    for (Counter* c = counter_list; c; c = c->parent) {
        if (strcmp(c->name, name) == 0) {
            return c;
        }
    }
    return nullptr;
}

Counter* Digester::create_counter(const char* name, Counter* parent) {
    Counter* counter = (Counter*)arena_alloc(arena, sizeof(Counter));
    memset(counter, 0, sizeof(Counter));
    
    size_t name_len = strlen(name);
    char* name_copy = (char*)arena_alloc(arena, name_len + 1);
    memcpy(name_copy, name, name_len + 1);
    
    counter->name = name_copy;
    counter->value = 0;
    counter->parent = parent ? parent : counter_list;
    counter->format = "arabic";
    
    // prepend to list (using parent field temporarily as next)
    Counter* old_list = counter_list;
    counter_list = counter;
    counter->parent = old_list;
    
    return counter;
}

void Digester::step_counter(const char* name) {
    Counter* counter = get_counter(name);
    if (!counter) {
        counter = create_counter(name);
    }
    counter->value++;
    log_debug("digester: step counter %s to %d", name, counter->value);
}

void Digester::add_to_counter(const char* name, int delta) {
    Counter* counter = get_counter(name);
    if (!counter) {
        counter = create_counter(name);
    }
    counter->value += delta;
}

void Digester::set_counter(const char* name, int value) {
    Counter* counter = get_counter(name);
    if (!counter) {
        counter = create_counter(name);
    }
    counter->value = value;
}

int Digester::get_counter_value(const char* name) const {
    for (Counter* c = counter_list; c; c = c->parent) {
        if (strcmp(c->name, name) == 0) {
            return c->value;
        }
    }
    return 0;
}

const char* Digester::format_counter(const char* name) {
    Counter* counter = get_counter(name);
    if (!counter) return "";
    
    strbuf_reset(strbuf);
    
    if (strcmp(counter->format, "arabic") == 0) {
        strbuf_append_int(strbuf, counter->value);
    } else if (strcmp(counter->format, "roman") == 0) {
        static const char* ones[] = {"", "i", "ii", "iii", "iv", "v", "vi", "vii", "viii", "ix"};
        static const char* tens[] = {"", "x", "xx", "xxx", "xl", "l", "lx", "lxx", "lxxx", "xc"};
        static const char* huns[] = {"", "c", "cc", "ccc", "cd", "d", "dc", "dcc", "dccc", "cm"};
        
        int val = counter->value;
        if (val > 0 && val < 4000) {
            while (val >= 1000) { strbuf_append_char(strbuf, 'm'); val -= 1000; }
            strbuf_append_str(strbuf, huns[val / 100]); val %= 100;
            strbuf_append_str(strbuf, tens[val / 10]); val %= 10;
            strbuf_append_str(strbuf, ones[val]);
        }
    } else if (strcmp(counter->format, "Roman") == 0) {
        static const char* ones[] = {"", "I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX"};
        static const char* tens[] = {"", "X", "XX", "XXX", "XL", "L", "LX", "LXX", "LXXX", "XC"};
        static const char* huns[] = {"", "C", "CC", "CCC", "CD", "D", "DC", "DCC", "DCCC", "CM"};
        
        int val = counter->value;
        if (val > 0 && val < 4000) {
            while (val >= 1000) { strbuf_append_char(strbuf, 'M'); val -= 1000; }
            strbuf_append_str(strbuf, huns[val / 100]); val %= 100;
            strbuf_append_str(strbuf, tens[val / 10]); val %= 10;
            strbuf_append_str(strbuf, ones[val]);
        }
    } else if (strcmp(counter->format, "alph") == 0) {
        if (counter->value >= 1 && counter->value <= 26) {
            char c = 'a' + counter->value - 1;
            strbuf_append_char(strbuf, c);
        }
    } else if (strcmp(counter->format, "Alph") == 0) {
        if (counter->value >= 1 && counter->value <= 26) {
            char c = 'A' + counter->value - 1;
            strbuf_append_char(strbuf, c);
        }
    }
    
    char* result = (char*)arena_alloc(arena, strbuf->length + 1);
    memcpy(result, strbuf->str, strbuf->length + 1);
    
    return result;
}

// ============================================================================
// Label/Reference Management
// ============================================================================

void Digester::set_label(const char* label) {
    size_t len = strlen(label);
    char* label_copy = (char*)arena_alloc(arena, len + 1);
    memcpy(label_copy, label, len + 1);
    
    pending_label = label_copy;
    log_debug("digester: set pending label '%s'", label);
}

const char* Digester::resolve_ref(const char* label) const {
    for (LabelList* l = label_list; l; l = l->next) {
        if (strcmp(l->entry.label, label) == 0) {
            return l->entry.ref_text;
        }
    }
    log_warn("digester: unresolved reference '%s'", label);
    return "??";
}

const char* Digester::resolve_pageref(const char* label) const {
    for (LabelList* l = label_list; l; l = l->next) {
        if (strcmp(l->entry.label, label) == 0) {
            return l->entry.page_text ? l->entry.page_text : "??";
        }
    }
    log_warn("digester: unresolved pageref '%s'", label);
    return "??";
}

// ============================================================================
// Footnotes
// ============================================================================

void Digester::add_footnote(DigestedNode* content) {
    if (footnote_count >= footnote_capacity) {
        size_t new_cap = footnote_capacity == 0 ? 8 : footnote_capacity * 2;
        DigestedNode** new_arr = (DigestedNode**)arena_alloc(
            arena, new_cap * sizeof(DigestedNode*));
        
        if (footnotes) {
            memcpy(new_arr, footnotes, footnote_count * sizeof(DigestedNode*));
        }
        
        footnotes = new_arr;
        footnote_capacity = new_cap;
    }
    
    footnotes[footnote_count++] = content;
    log_debug("digester: added footnote %zu", footnote_count);
}

DigestedNode** Digester::get_footnotes(size_t* count) {
    *count = footnote_count;
    return footnotes;
}

void Digester::clear_footnotes() {
    footnote_count = 0;
}

// ============================================================================
// Output Building
// ============================================================================

void Digester::add_node(DigestedNode* node) {
    if (!current_list) {
        log_error("digester: no current list to add node to");
        return;
    }
    current_list->append(node);
}

void Digester::add_text(const char* text, size_t len) {
    DigestedNode* box = DigestedNode::make_box(arena, text, len, font);
    add_node(box);
}

void Digester::add_char(int32_t codepoint) {
    DigestedNode* chr = DigestedNode::make_char(arena, codepoint, font);
    add_node(chr);
}

void Digester::add_glue(const GlueSpec& spec) {
    DigestedNode* glue = DigestedNode::make_glue(arena, spec);
    add_node(glue);
}

void Digester::add_kern(float amount) {
    DigestedNode* kern = DigestedNode::make_kern(arena, amount);
    add_node(kern);
}

void Digester::add_penalty(int value) {
    DigestedNode* penalty = DigestedNode::make_penalty(arena, value);
    add_node(penalty);
}

void Digester::add_rule(float width, float height, float depth) {
    DigestedNode* rule = DigestedNode::make_rule(arena, width, height, depth);
    add_node(rule);
}

void Digester::add_mark(const char* text, size_t len, int mark_class) {
    DigestedNode* mark = DigestedNode::make_mark(arena, text, len, mark_class);
    add_node(mark);
}

void Digester::add_special(const char* command, size_t len) {
    DigestedNode* special = DigestedNode::make_special(arena, command, len);
    add_node(special);
}

// ============================================================================
// Grouping
// ============================================================================

void Digester::begin_group() {
    group_level++;
    
    GroupSave* save = (GroupSave*)arena_alloc(arena, sizeof(GroupSave));
    save->font = font;
    save->mode = current_mode;
    save->prev = group_stack;
    group_stack = save;
    
    if (registry) {
        registry->begin_group();
    }
    
    log_debug("digester: begin group (level %d)", group_level);
}

void Digester::end_group() {
    if (group_level <= 0) {
        error("too many }'s");
        return;
    }
    
    if (group_stack) {
        font = group_stack->font;
        group_stack = group_stack->prev;
    }
    
    if (registry) {
        registry->end_group();
    }
    
    group_level--;
    log_debug("digester: end group (level %d)", group_level);
}

// ============================================================================
// Error Handling
// ============================================================================

void Digester::error(const char* message) {
    log_error("digester error: %s", message);
}

void Digester::warning(const char* message) {
    log_warn("digester warning: %s", message);
}

// ============================================================================
// Argument Reading
// ============================================================================

DigestedNode* Digester::read_argument() {
    Token t;
    do {
        t = expander->expand_token();
    } while (t.has_catcode(CatCode::SPACE));
    
    if (t.is_end()) {
        error("unexpected end of input reading argument");
        return nullptr;
    }
    
    if (t.has_catcode(CatCode::BEGIN_GROUP)) {
        return digest_group();
    } else {
        DigestedNode* list = DigestedNode::make_list(arena, true);
        push_list(list);
        digest_token(t);
        pop_list();
        return list;
    }
}

DigestedNode* Digester::read_optional_argument() {
    Token t;
    do {
        t = expander->expand_token();
    } while (t.has_catcode(CatCode::SPACE));
    
    if (t.is_end()) {
        return nullptr;
    }
    
    if (t.is_char() && t.chr.ch == '[') {
        DigestedNode* list = DigestedNode::make_list(arena, true);
        push_list(list);
        
        int depth = 0;
        while (!expander->at_end()) {
            t = expander->expand_token();
            if (t.is_end()) break;
            
            if (t.is_char() && t.chr.ch == '[') {
                depth++;
                digest_token(t);
            } else if (t.is_char() && t.chr.ch == ']') {
                if (depth == 0) break;
                depth--;
                digest_token(t);
            } else {
                digest_token(t);
            }
        }
        
        pop_list();
        return list;
    } else {
        expander->push_back(t);
        return nullptr;
    }
}

TokenList Digester::read_balanced_text() {
    TokenList result;
    
    Token t = expander->get_token();
    if (!t.has_catcode(CatCode::BEGIN_GROUP)) {
        error("expected { in balanced text");
        return result;
    }
    
    int depth = 1;
    while (!expander->at_end() && depth > 0) {
        t = expander->get_token();
        if (t.is_end()) break;
        
        if (t.has_catcode(CatCode::BEGIN_GROUP)) {
            depth++;
            result.push_back(t);
        } else if (t.has_catcode(CatCode::END_GROUP)) {
            depth--;
            if (depth > 0) {
                result.push_back(t);
            }
        } else {
            result.push_back(t);
        }
    }
    
    return result;
}

// ============================================================================
// Internal Helpers
// ============================================================================

void Digester::push_list(DigestedNode* list) {
    ListContext* ctx = (ListContext*)arena_alloc(arena, sizeof(ListContext));
    ctx->list = current_list;
    ctx->mode = current_mode;
    ctx->prev = list_stack;
    list_stack = ctx;
    current_list = list;
}

DigestedNode* Digester::pop_list() {
    if (!list_stack) {
        error("list stack underflow");
        return current_list;
    }
    
    DigestedNode* result = current_list;
    current_list = list_stack->list;
    list_stack = list_stack->prev;
    
    return result;
}

void Digester::save_font() {
    // font is automatically saved in begin_group
}

void Digester::restore_font() {
    // font is automatically restored in end_group
}

void Digester::process_character(const Token& token) {
    if (!token.is_char()) return;
    
    char c = token.chr.ch;
    CatCode cat = token.catcode;
    
    switch (cat) {
        case CatCode::SPACE:
            process_space();
            break;
            
        case CatCode::LETTER:
        case CatCode::OTHER:
            if (is_vertical()) {
                begin_paragraph();
            }
            add_char((int32_t)(unsigned char)c);
            break;
            
        case CatCode::BEGIN_GROUP:
            process_begin_group();
            break;
            
        case CatCode::END_GROUP:
            process_end_group();
            break;
            
        case CatCode::MATH_SHIFT:
            process_math_shift();
            break;
            
        case CatCode::SUPERSCRIPT:
        case CatCode::SUBSCRIPT:
            if (!is_math()) {
                if (is_vertical()) {
                    begin_paragraph();
                }
                add_char((int32_t)(unsigned char)c);
            }
            break;
            
        case CatCode::ALIGN_TAB:
            add_char('&');
            break;
            
        case CatCode::PARAM:
            add_char('#');
            break;
            
        default:
            break;
    }
}

void Digester::process_space() {
    if (is_horizontal() || is_math()) {
        GlueSpec space_glue;
        space_glue.space = font.size_pt * 0.333f;
        space_glue.stretch = font.size_pt * 0.166f;
        space_glue.shrink = font.size_pt * 0.111f;
        add_glue(space_glue);
    }
}

void Digester::process_math_shift() {
    if (is_math()) {
        bool was_display = (current_mode == DigesterMode::MATH);
        end_math();
        // if it was display math, consume the second $
        if (was_display) {
            Token next = expander->get_token();
            if (!next.has_catcode(CatCode::MATH_SHIFT)) {
                // error - display math must close with $$
                error("display math must close with $$");
                expander->push_back(next);
            }
        }
    } else {
        Token next = expander->get_token();
        bool display = false;
        
        if (next.has_catcode(CatCode::MATH_SHIFT)) {
            display = true;
        } else {
            expander->push_back(next);
        }
        
        begin_math(display);
    }
}

void Digester::process_begin_group() {
    begin_group();
}

void Digester::process_end_group() {
    end_group();
}

void Digester::process_control_sequence(const Token& token) {
    if (!token.is_cs() && !token.is_active()) return;
    
    const char* name = token.is_cs() ? token.cs.name : nullptr;
    size_t name_len = token.is_cs() ? token.cs.len : 0;
    
    // special handling for \par
    if (name && name_len == 3 && memcmp(name, "par", 3) == 0) {
        if (is_horizontal()) {
            end_paragraph();
        }
        return;
    }
    
    // check command registry
    if (registry) {
        const CommandDef* def = registry->lookup(name, name_len);
        if (def) {
            switch (def->type) {
                case CommandType::PRIMITIVE:
                    execute_primitive(def);
                    return;
                    
                case CommandType::CONSTRUCTOR: {
                    DigestedNode* node = execute_constructor(def);
                    if (node) {
                        if (is_vertical() && 
                            (node->type != DigestedType::WHATSIT || 
                             !(node->flags & DigestedNode::FLAG_VERTICAL))) {
                            begin_paragraph();
                        }
                        add_node(node);
                    }
                    return;
                }
                    
                case CommandType::MACRO:
                    expand_macro(def);
                    return;
                    
                case CommandType::ENVIRONMENT:
                    break;
                    
                case CommandType::MATH:
                    if (!is_math()) {
                            log_warn("digester: math command used outside math mode");
                    }
                    {
                        DigestedNode* whatsit = DigestedNode::make_whatsit(
                            arena, name, name_len);
                        whatsit->set_property("role", def->pattern);
                        whatsit->set_property("meaning", def->replacement);
                        add_node(whatsit);
                    }
                    return;
            }
        }
    }
    
    const CommandEntry* entry = expander->lookup(token);
    if (entry && entry->type != CommandEntry::Type::UNDEFINED) {
        log_debug("digester: unhandled control sequence \\%.*s", 
                  (int)name_len, name);
    } else {
        log_debug("digester: undefined control sequence \\%.*s",
                  (int)name_len, name);
    }
}

void Digester::execute_primitive(const CommandDef* def) {
    if (def->use_callback && def->callback) {
        ((PrimitiveFn)def->callback)(this);
    }
}

DigestedNode* Digester::execute_constructor(const CommandDef* def) {
    DigestedNode** args = nullptr;
    size_t arg_count = 0;
    
    if (def->param_count > 0) {
        args = (DigestedNode**)arena_alloc(arena, 
                                           def->param_count * sizeof(DigestedNode*));
        
        const char* p = def->params;
        while (*p && arg_count < (size_t)def->param_count) {
            if (*p == '[') {
                args[arg_count++] = read_optional_argument();
                while (*p && *p != ']') p++;
                if (*p == ']') p++;
            } else if (*p == '{') {
                args[arg_count++] = read_argument();
                p++;
                while (*p && *p != '}') p++;
                if (*p == '}') p++;
            } else {
                p++;
            }
        }
    }
    
    DigestedNode* result = nullptr;
    
    if (def->before_digest) {
        def->before_digest(this, nullptr);
    }
    
    if (def->use_callback && def->callback) {
        result = def->callback(this, args, arg_count);
    } else if (def->pattern) {
        result = DigestedNode::make_whatsit(arena, def->name, def->name_len);
        result->content.whatsit.definition = def;
        result->content.whatsit.args = args;
        result->content.whatsit.arg_count = arg_count;
        result->set_property("pattern", def->pattern);
    }
    
    if (def->after_digest) {
        def->after_digest(this, result);
    }
    
    return result;
}

void Digester::expand_macro(const CommandDef* def) {
    log_debug("digester: would expand macro \\%s", def->name);
}

// ============================================================================
// PackageLoader Implementation
// ============================================================================

PackageLoader::PackageLoader(CommandRegistry* reg, Arena* a)
    : registry(reg)
    , arena(a)
    , loaded_packages(0)
{
}

void PackageLoader::load_tex_base() {
    if (loaded_packages & PKG_TEX_BASE) return;
    
    log_debug("package: loading tex_base");
    register_tex_primitives();
    
    loaded_packages |= PKG_TEX_BASE;
}

void PackageLoader::load_latex_base() {
    if (loaded_packages & PKG_LATEX_BASE) return;
    
    load_tex_base();
    
    log_debug("package: loading latex_base");
    register_latex_commands();
    
    loaded_packages |= PKG_LATEX_BASE;
}

void PackageLoader::load_amsmath() {
    if (loaded_packages & PKG_AMSMATH) return;
    
    load_latex_base();
    
    log_debug("package: loading amsmath");
    register_ams_commands();
    
    loaded_packages |= PKG_AMSMATH;
}

bool PackageLoader::load_package(const char* name) {
    if (is_loaded(name)) return true;
    
    if (strcmp(name, "tex_base") == 0) {
        load_tex_base();
        return true;
    }
    if (strcmp(name, "latex_base") == 0 || strcmp(name, "latex") == 0) {
        load_latex_base();
        return true;
    }
    if (strcmp(name, "amsmath") == 0) {
        load_amsmath();
        return true;
    }
    if (strcmp(name, "amssymb") == 0) {
        load_amsmath();
        loaded_packages |= PKG_AMSSYMB;
        return true;
    }
    
    log_warn("package: unknown package '%s'", name);
    return false;
}

bool PackageLoader::is_loaded(const char* name) const {
    if (strcmp(name, "tex_base") == 0) return loaded_packages & PKG_TEX_BASE;
    if (strcmp(name, "latex_base") == 0) return loaded_packages & PKG_LATEX_BASE;
    if (strcmp(name, "amsmath") == 0) return loaded_packages & PKG_AMSMATH;
    if (strcmp(name, "amssymb") == 0) return loaded_packages & PKG_AMSSYMB;
    return false;
}

// ============================================================================
// Primitive Registration
// ============================================================================

static void prim_relax(Digester* d) {
    // do nothing
}

static void prim_par(Digester* d) {
    if (d->is_horizontal()) {
        d->end_paragraph();
    }
}

static void prim_indent(Digester* d) {
    if (!d->is_horizontal()) {
        d->begin_paragraph();
    }
    d->add_kern(d->current_font().size_pt * 1.5f);
}

static void prim_noindent(Digester* d) {
    if (!d->is_horizontal()) {
        d->begin_paragraph();
    }
}

void PackageLoader::register_tex_primitives() {
    registry->define_primitive("relax", "", prim_relax);
    registry->define_primitive("par", "", prim_par);
    registry->define_primitive("indent", "", prim_indent);
    registry->define_primitive("noindent", "", prim_noindent);
}

// ============================================================================
// LaTeX Command Registration
// ============================================================================

static DigestedNode* ctor_section(Digester* d, DigestedNode** args, size_t arg_count) {
    DigestedNode* whatsit = DigestedNode::make_whatsit(d->get_arena(), "section", 7);
    
    d->step_counter("section");
    whatsit->set_property("number", d->format_counter("section"));
    
    if (arg_count > 0 && args[0]) {
        whatsit->content.whatsit.args = args;
        whatsit->content.whatsit.arg_count = 1;
    }
    
    return whatsit;
}

static DigestedNode* ctor_textbf(Digester* d, DigestedNode** args, size_t arg_count) {
    if (arg_count == 0 || !args[0]) return nullptr;
    
    DigestedNode* whatsit = DigestedNode::make_whatsit(d->get_arena(), "textbf", 6);
    whatsit->content.whatsit.args = args;
    whatsit->content.whatsit.arg_count = 1;
    whatsit->set_property("style", "bold");
    
    return whatsit;
}

static DigestedNode* ctor_textit(Digester* d, DigestedNode** args, size_t arg_count) {
    if (arg_count == 0 || !args[0]) return nullptr;
    
    DigestedNode* whatsit = DigestedNode::make_whatsit(d->get_arena(), "textit", 6);
    whatsit->content.whatsit.args = args;
    whatsit->content.whatsit.arg_count = 1;
    whatsit->set_property("style", "italic");
    
    return whatsit;
}

static DigestedNode* ctor_emph(Digester* d, DigestedNode** args, size_t arg_count) {
    if (arg_count == 0 || !args[0]) return nullptr;
    
    DigestedNode* whatsit = DigestedNode::make_whatsit(d->get_arena(), "emph", 4);
    whatsit->content.whatsit.args = args;
    whatsit->content.whatsit.arg_count = 1;
    whatsit->set_property("style", "emphasis");
    
    return whatsit;
}

static DigestedNode* ctor_frac(Digester* d, DigestedNode** args, size_t arg_count) {
    if (arg_count < 2) return nullptr;
    
    DigestedNode* whatsit = DigestedNode::make_whatsit(d->get_arena(), "frac", 4);
    whatsit->content.whatsit.args = args;
    whatsit->content.whatsit.arg_count = 2;
    whatsit->flags |= DigestedNode::FLAG_MATH;
    
    return whatsit;
}

void PackageLoader::register_latex_commands() {
    registry->define_constructor_fn("section", "{}", ctor_section);
    registry->define_constructor("section*", "{}", "<section class=\"unnumbered\"><title>#1</title>");
    
    registry->define_constructor_fn("textbf", "{}", ctor_textbf);
    registry->define_constructor_fn("textit", "{}", ctor_textit);
    registry->define_constructor_fn("emph", "{}", ctor_emph);
    registry->define_constructor("texttt", "{}", "<code>#1</code>");
    
    registry->define_environment("itemize", "<ul>", "</ul>");
    registry->define_environment("enumerate", "<ol>", "</ol>");
    registry->define_environment("center", "<div class=\"center\">", "</div>");
    
    registry->define_constructor_fn("frac", "{}{}", ctor_frac);
}

void PackageLoader::register_ams_commands() {
    registry->define_environment("align", "<math-align numbered=\"true\">", "</math-align>");
    registry->define_environment("align*", "<math-align>", "</math-align>");
    registry->define_environment("cases", "<math-cases>", "</math-cases>");
    registry->define_environment("matrix", "<matrix>", "</matrix>");
    registry->define_environment("pmatrix", "<matrix delimiters=\"()\">", "</matrix>");
    registry->define_environment("bmatrix", "<matrix delimiters=\"[]\">", "</matrix>");
    
    registry->define_math("sin", "sin", "TRIGFUNCTION");
    registry->define_math("cos", "cos", "TRIGFUNCTION");
    registry->define_math("tan", "tan", "TRIGFUNCTION");
    registry->define_math("log", "log", "FUNCTION");
    registry->define_math("lim", "limit", "LIMITOP");
    registry->define_math("sum", "sum", "SUMOP");
    registry->define_math("int", "integral", "INTOP");
}

} // namespace tex
