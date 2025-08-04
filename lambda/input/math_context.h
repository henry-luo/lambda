// Math Context Management for Enhanced Markdown Integration
// This file provides a math parsing context that can be cached and reused 
// for consecutive math expressions within the same document

#ifndef MATH_CONTEXT_H
#define MATH_CONTEXT_H

#include "input.h"

// Math parsing context structure
typedef struct MathContext {
    Input* base_input;              // Reference to the main document input
    VariableMemPool* shared_pool;   // Shared memory pool for math expressions
    char* current_flavor;           // Current math flavor (latex, typst, ascii)
    void* parser_state;             // Internal parser state 
    int expression_count;           // Number of expressions parsed so far
    bool context_valid;             // Whether the context is still valid
} MathContext;

// Create a new math context for a document
MathContext* math_context_create(Input* document_input, const char* flavor);

// Parse a math expression using the cached context
Item math_context_parse_expression(MathContext* ctx, const char* math_string);

// Clear any temporary state but keep the context for reuse
void math_context_reset_state(MathContext* ctx);

// Destroy the math context and free resources
void math_context_destroy(MathContext* ctx);

// Check if context can be reused for another expression
bool math_context_is_valid(MathContext* ctx);

#endif // MATH_CONTEXT_H
