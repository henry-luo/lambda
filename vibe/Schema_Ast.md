# Schema AST Integration Plan

**Date:** September 10, 2025  
**Author:** Analysis of Lambda Codebase  
**Status:** Revised - Ready for Implementation

## Executive Summary

This document outlines the plan to **completely remove** `lambda/validator/schema_parser.cpp` and refactor the validator to use AST built directly by `lambda/build_ast.cpp`. The validator will operate on native Lambda AST structures (TypeMap, TypeArray, TypeElmt) instead of custom schema types, eliminating ~1000 lines of duplicate code.

## Current State Analysis

### Schema Parser (`schema_parser.cpp`) - Current Issues
- **Duplicated Infrastructure**: Reimplements type building logic already present in AST builder
- **Custom Type System**: Uses `TypeSchema`, `SchemaMap`, `SchemaElement` structs instead of Lambda's native types
- **Memory Management**: Uses separate memory pool management instead of transpiler's `ast_pool`
- **Tree-sitter Integration**: Duplicates Tree-sitter parsing logic from main transpiler
- **Key Problems**:
  - `SchemaMap` vs `TypeMap` - duplicate map type representations
  - `SchemaElement` vs `TypeElmt` - duplicate element type representations  
  - `SchemaArray` vs `TypeArray` - duplicate array type representations
  - Custom `build_schema_type()` vs existing `build_*_type()` functions
  - Separate type registry vs transpiler's existing type system

### AST Builder (`build_ast.cpp`) - Existing Capabilities
- **Mature Infrastructure**: Complete type building system with `TypeMap`, `TypeArray`, `TypeElmt`
- **ShapeEntry System**: Already supports field definitions with names, types, and validation
- **Occurrence Support**: `build_occurrence_type()` already exists for `Type?`, `Type+`, `Type*`
- **Union Types**: `build_binary_type()` supports `Type1 | Type2` via binary expressions
- **Memory Management**: Integrated with transpiler's `ast_pool` for efficient allocation
- **Key Advantages**:
  - `TypeMap` with `ShapeEntry` linked list for field definitions
  - `TypeElmt` with attribute support via `ShapeEntry` system
  - `TypeArray` with occurrence modifiers already implemented
  - Unified memory pool management
  - Integration with Lambda's runtime type system

## Complete Removal Goals

1. **Delete schema_parser.cpp**: Remove entire file (~1644 lines of duplicate code)
2. **Refactor Validator API**: Make validator operate directly on AST built by `build_ast.cpp`
3. **Eliminate Custom Schema Types**: Remove `TypeSchema`, `SchemaMap`, `SchemaElement` structs
4. **Direct AST Validation**: Validate against native `TypeMap`, `TypeArray`, `TypeElmt` structures
5. **Preserve Public API**: Keep validator interface unchanged for external users

## Complete Removal Architecture

### 1. Validator Refactoring Strategy

**Remove schema_parser.cpp and Use AST Directly:**

```cpp
// BEFORE: Validator uses custom schema types
ValidationResult* validate_map(SchemaValidator* validator, TypedItem typed_item, 
                              TypeSchema* schema, ValidationContext* ctx);

// AFTER: Validator uses AST types directly  
ValidationResult* validate_map(SchemaValidator* validator, TypedItem typed_item,
                              TypeMap* map_type, ValidationContext* ctx);

// BEFORE: Schema parsing creates TypeSchema
TypeSchema* parse_schema_from_source(SchemaParser* parser, const char* source);

// AFTER: Use transpiler to build AST directly
AstNode* build_schema_ast(Transpiler* tp, const char* source);
TypeMap* extract_map_type(AstNode* ast_node);
```

### 2. Direct AST Usage Strategy

**Use Transpiler to Build Schema AST:**

```cpp
// NEW: Schema validator uses transpiler directly
typedef struct SchemaValidator {
    Transpiler* transpiler;        // Use existing transpiler instead of SchemaParser
    VariableMemPool* pool;         // Memory pool
    ValidationContext* context;    // Validation context
    ValidationOptions default_options;
} SchemaValidator;

// NEW: Load schema by building AST
int schema_validator_load_schema(SchemaValidator* validator, const char* schema_source, 
                                const char* schema_name) {
    // Use existing transpiler to build AST
    AstNode* schema_ast = transpiler_build_ast(validator->transpiler, schema_source);
    
    // Extract type definitions from AST
    extract_type_definitions_from_ast(validator, schema_ast, schema_name);
    return 0;
}
```

**Key Benefits:**
- No duplicate parsing logic - use existing `transpiler_build_ast()`
- Direct access to `TypeMap`, `TypeArray`, `TypeElmt` from AST
- Leverage existing type building functions in `build_ast.cpp`
- Use transpiler's optimized memory management

### 3. Complete SchemaParser Elimination

**Delete SchemaParser - Use Transpiler Directly:**

```cpp
// BEFORE: Custom SchemaParser (DELETE ENTIRE STRUCT)
typedef struct SchemaParser {
    Transpiler base;             // DUPLICATE - delete
    HashMap* type_registry;      // DUPLICATE - delete  
    ArrayList* type_definitions; // DUPLICATE - delete
    VariableMemPool* pool;       // DUPLICATE - delete
    const char* current_source;  // DUPLICATE - delete
    TSTree* current_tree;        // DUPLICATE - delete
} SchemaParser;

// AFTER: Use existing Transpiler directly
SchemaValidator* validator = schema_validator_create(pool);
Transpiler* tp = transpiler_create(pool);

// Build schema AST using existing transpiler
AstNode* schema_ast = transpiler_build_ast(tp, schema_source);

// Extract types from AST for validation
extract_schema_types_from_ast(validator, schema_ast);
```

**Complete Elimination:**
- Delete entire `schema_parser.cpp` file (1644 lines)
- Remove all `build_schema_*()` functions - use existing `build_*()` functions
- Remove all `TypeSchema`, `SchemaMap`, `SchemaElement` structs
- Use transpiler's existing infrastructure for everything

### 4. Direct AST Validation

**Validate Against Native AST Types:**

```cpp
// NEW: Validation functions operate on AST types directly
ValidationResult* validate_against_type_map(TypedItem item, TypeMap* map_type, ValidationContext* ctx);
ValidationResult* validate_against_type_array(TypedItem item, TypeArray* array_type, ValidationContext* ctx);  
ValidationResult* validate_against_type_elmt(TypedItem item, TypeElmt* elmt_type, ValidationContext* ctx);

// NEW: Extract validation info from existing AST structures
bool is_map_field_required(TypeMap* map_type, StrView field_name);
bool is_element_attribute_required(TypeElmt* elmt_type, StrView attr_name);
TypeUnary* get_occurrence_info(Type* type); // Use existing TypeUnary for occurrence modifiers

// NEW: Schema registry using AST types
typedef struct SchemaRegistry {
    HashMap* type_definitions;   // name -> AstNode* mapping
    Transpiler* transpiler;      // Transpiler that built the AST
} SchemaRegistry;
```

## Implementation Plan

### Phase 1: Complete schema_parser.cpp Removal (Week 1-2)
1. **Delete schema_parser.cpp**
   - Remove entire file (1644 lines)
   - Delete all `build_schema_*()` functions
   - Remove `SchemaParser` struct definition
   - Clean up includes and references

2. **Refactor SchemaValidator to Use Transpiler**
   - Replace `SchemaParser* parser` with `Transpiler* transpiler` in `SchemaValidator`
   - Modify `schema_validator_load_schema()` to use `transpiler_build_ast()`
   - Update schema loading to extract types from AST instead of custom parsing

### Phase 2: Validator API Refactoring (Week 3-4)
1. **Remove Custom Schema Types from validator.hpp**
   - Delete `TypeSchema`, `SchemaMap`, `SchemaElement`, `SchemaArray` structs
   - Remove all `build_schema_*()` function declarations
   - Update validation functions to use AST types (`TypeMap*`, `TypeElmt*`, etc.)

2. **Implement AST-based Validation**
   - Create `validate_against_type_map()`, `validate_against_type_elmt()` functions
   - Extract validation info from `ShapeEntry` linked lists
   - Use existing `TypeUnary` for occurrence validation
   - Implement type extraction from AST nodes

### Phase 3: API Compatibility (Week 5-6)
1. **Preserve Public API**
   - Keep `LambdaValidator` public interface unchanged
   - Maintain `lambda_validate_file()`, `lambda_validate_string()` functions
   - Ensure `LambdaValidationResult` structure remains compatible
   - Update internal implementation to use AST without breaking external API

2. **Schema Loading Refactoring**
   - Replace schema parsing with AST building
   - Extract type definitions from AST nodes
   - Store AST types in schema registry instead of custom types

### Phase 4: Testing and Cleanup (Week 7-8)
1. **Comprehensive Testing**
   - Ensure all existing validator tests pass unchanged
   - Verify performance improvement from eliminating duplicate code
   - Test schema loading with complex type definitions

2. **Documentation and Cleanup**
   - Update `schema_ast.hpp` to reflect AST-only architecture
   - Remove all references to deleted `schema_parser.cpp`
   - Document new AST-based validation approach

## Technical Advantages of Refactoring

### 1. Eliminate Code Duplication
**Current Problem**: `SchemaMap` vs `TypeMap`, `SchemaElement` vs `TypeElmt` duplicate functionality
**Solution**: Use existing AST types that already support the required features

### 2. Leverage Existing Infrastructure
**Current Problem**: Separate Tree-sitter parser, memory pools, and type registries
**Solution**: Use transpiler's existing infrastructure that's already optimized and tested

### 3. ShapeEntry System Already Supports Schema Needs
**Discovery**: `TypeMap` and `TypeElmt` already use `ShapeEntry` linked lists for field definitions
**Benefit**: No need to create new field definition system - it already exists!

### 4. Occurrence Support Already Exists
**Discovery**: `build_occurrence_type()` already handles `Type?`, `Type+`, `Type*` syntax
**Benefit**: No need to implement occurrence modifiers - they're already working!

### 5. Union Type Support Already Exists
**Discovery**: `build_binary_type()` already supports `Type1 | Type2` via binary expressions
**Benefit**: Union types work out of the box with existing AST builder

## File Structure Changes

### Modified Files
- `lambda/validator/schema_parser.cpp` - Refactor to use AST builder functions
  - Replace `build_schema_type()` with calls to existing `build_*_type()` functions
  - Use `TypeMap`, `TypeArray`, `TypeElmt` instead of custom schema structs
  - Use transpiler's `ast_pool` instead of separate memory management
- `lambda/schema_ast.hpp` - Simplify to use existing AST types
  - Remove duplicate type definitions
  - Focus on validation-specific extensions to existing types
- `lambda/validator/validator.hpp` - Update to use AST types

### No New Files Needed
- Existing `build_ast.cpp` already has all required type building functions
- Existing AST type system already supports schema validation requirements
- Existing transpiler infrastructure already provides needed capabilities

### Key Insight: Minimal Changes Required
- Most functionality already exists in AST builder
- Refactoring is primarily about removing duplication, not adding new features
- Schema parser can be simplified to a thin wrapper around existing AST builder

## Benefits of Refactoring

1. **Eliminate Code Duplication**: Remove ~800 lines of duplicate type building logic
2. **Leverage Existing Optimizations**: AST builder is already optimized and battle-tested
3. **Unified Memory Management**: Single `ast_pool` instead of multiple memory pools
4. **Simplified Maintenance**: One type system instead of two parallel systems
5. **Better Performance**: No overhead from duplicate type structures and parsing
6. **Existing Features Work**: Occurrence modifiers, union types, field definitions already implemented

## Migration Strategy

1. **Transparent Refactoring**: Schema validation API remains unchanged
2. **Internal Implementation**: Replace custom structs with AST types internally
3. **Validation Compatibility**: All existing validator tests should pass unchanged
4. **Performance Improvement**: Users benefit from better performance without code changes

## Success Metrics

1. **Code Reduction**: Eliminate ~800 lines of duplicate code from schema_parser.cpp
2. **Performance Improvement**: Faster validation due to single type system
3. **Memory Efficiency**: Reduced memory usage from eliminating duplicate type structures
4. **Test Compatibility**: All existing validator tests pass without modification
5. **Maintainability**: Single type system easier to maintain and extend

## Key Implementation Insights

### 1. AST Builder Already Has Required Features
- `build_occurrence_type()` exists for `Type?`, `Type+`, `Type*`
- `build_binary_type()` supports union types `Type1 | Type2`
- `ShapeEntry` system in `TypeMap`/`TypeElmt` supports field definitions
- `TypeArray`, `TypeMap`, `TypeElmt` match schema requirements exactly

### 2. Refactoring is Simplification, Not Extension
- Remove custom `TypeSchema`, `SchemaMap`, `SchemaElement` structs
- Replace `build_schema_type()` with existing `build_*_type()` function calls
- Use transpiler's `ast_pool` instead of separate memory management
- Leverage existing type registry in transpiler's `type_list`

### 3. Minimal API Changes Required
- Schema validation interface can remain unchanged
- Internal implementation uses AST types instead of custom types
- Performance improves due to elimination of duplicate structures
- Maintenance burden reduces significantly

## Next Steps

1. **Phase 1**: Start refactoring `schema_parser.cpp` to use AST builder functions
2. **Validation**: Ensure all existing validator tests continue to pass
3. **Performance Testing**: Measure improvement from eliminating duplicate type system
4. **Documentation**: Update schema_ast.hpp to reflect simplified architecture

---

*This refactoring plan leverages existing Lambda AST infrastructure to eliminate code duplication and improve performance while maintaining full schema validation capabilities.*
