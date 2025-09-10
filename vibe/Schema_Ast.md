# Schema AST Integration Plan

**Date:** September 10, 2025  
**Author:** GitHub Copilot  
**Status:** Planning Ph2. **Merge Type Building Functions**
   - Integrate `build_schema_type()` logic into existing builders
   - Enhance `build_binary_type()` for comprehensive union type support
   - **Add `build_occurrence_type()` function to handle `Type?`, `Type+`, `Type*` syntax**
   - Add occurrence support to array/list builders
   - Implement type reference resolution
## Executive Summary

This document outlines the plan to merge the standalone schema parser (`lambda/validator/schema_parser.cpp`) into the main Lambda AST builder (`lambda/build_ast.cpp`) to create a unified type system that supports full schema definitions within Lambda scripts.

## Current State Analysis

### Schema Parser (`schema_parser.cpp`)
- **Purpose**: Validates data against schema definitions using Tree-sitter parsing
- **Type System**: Rich schema types (primitives, unions, arrays, maps, elements, occurrences)
- **Key Features**:
  - Union types (`Type1 | Type2`)
  - Element schemas with attributes and content (`<tag attr: Type, Content*>`)
  - Occurrence modifiers (`Type?`, `Type+`, `Type*`)
  - Type references and definitions
  - Map schemas with field definitions
  - Content type validation

### AST Builder (`build_ast.cpp`)
- **Purpose**: Builds AST for Lambda script compilation and execution
- **Type System**: Runtime types focused on execution (TypeArray, TypeMap, TypeElmt, etc.)
- **Key Features**:
  - Function type building (`build_func_type`)
  - Collection types (`build_array_type`, `build_list_type`, `build_map_type`)
  - Element types (`build_element_type`)
  - Type inference and validation during compilation

## Integration Goals

1. **Unified Type System**: Merge schema validation capabilities into the main AST builder
2. **Enhanced Type Definitions**: Support rich schema syntax in Lambda scripts
3. **Compile-time Validation**: Enable schema validation during AST building
4. **Runtime Integration**: Bridge schema types with Lambda's runtime type system

## Proposed Architecture

### 1. Enhanced Type Node Structure

Extend the existing AST type nodes to support schema information:

```cpp
// Enhanced type node that bridges AST and schema
typedef struct AstSchemaNode : AstTypeNode {
    TypeSchema* schema_type;     // Schema validation info
    Type* runtime_type;          // Runtime execution type
    bool is_schema_definition;   // True for type definitions
    StrView type_name;           // For type references
} AstSchemaNode;
```

### 2. Unified Type Building Functions

Merge and enhance type building functions:

**Current Functions to Enhance:**
- `build_base_type()` → Support schema primitives and type references
- `build_array_type()` → Support occurrence modifiers (`Type*`, `Type+`, `Type?`)
- `build_map_type()` → Support field validation and open/closed maps
- `build_element_type()` → Support attribute schemas and content validation
- `build_binary_type()` → Already supports union types (`Type1 | Type2`) via `|` operator
- **Add `build_occurrence_type()`** → Support occurrence modifiers (`Type?`, `Type+`, `Type*`)
  - Grammar already defines `type_occurrence` and `sym_type_occurrence` exists
  - Need to add `AST_NODE_UNARY_TYPE` and `SYM_TYPE_OCCURRENCE` to ast.hpp
  - Need to handle `sym_type_occurrence` in `build_expr()` switch statement

### 3. Schema-aware AST Building

Integrate schema parsing into the main AST building pipeline:

```cpp
// Enhanced transpiler with schema support
typedef struct SchemaTranspiler : Transpiler {
    HashMap* type_registry;      // From schema_parser
    ArrayList* type_definitions; // From schema_parser
    bool schema_mode;            // Enable schema validation
} SchemaTranspiler;
```

### 4. Type Definition Support

Add support for type statements in Lambda scripts:

```lambda
// Type definitions that become part of the AST
Document = <html lang: string, 
    <head <title string*>, <meta name: string, content: string>*>?,
    <body Content*>
>

Person = {
    name: string,
    age: int?,
    email: string | null
}

Content = string | <p string*> | <div Content*>
```

## Implementation Plan

### Phase 1: Foundation (Week 1-2)
1. **Extract Common Infrastructure**
   - Move `TypeSchema` structures to shared header
   - Create unified type creation functions
   - Establish schema-to-runtime type mapping

2. **Extend AST Nodes**
   - Add `AstSchemaTypeNode` and related structures
   - Update `AstNodeType` enum with schema node types (`AST_NODE_UNARY_TYPE`)
   - Add `SYM_TYPE_OCCURRENCE` to ast.hpp symbol definitions
   - Modify type building function signatures

### Phase 2: Core Integration (Week 3-4)
1. **Merge Type Building Functions**
   - Integrate `build_schema_type()` logic into existing builders
   - Add union type and occurrence support
   - Implement type reference resolution

2. **Schema Definition Support**
   - Add `build_type_definition()` to main AST builder
   - Support `type_stam` nodes in the main expression builder
   - Implement type registry in the transpiler

### Phase 3: Validation Integration (Week 5-6)
1. **Compile-time Validation**
   - Integrate schema validation into type checking
   - Add validation error reporting to transpiler
   - Support schema inheritance and composition

2. **Runtime Bridge**
   - Map schema types to runtime types
   - Support dynamic schema validation
   - Implement schema-aware serialization

### Phase 4: Advanced Features (Week 7-8)
1. **Enhanced Schema Syntax**
   - Support complex element schemas
   - Add constraint validation
   - Implement schema imports/exports

2. **Optimization and Testing**
   - Performance optimization for large schemas
   - Comprehensive test suite
   - Documentation and examples

## Technical Challenges

### 1. Type System Unification
**Challenge**: Bridge the gap between schema validation types and runtime execution types
**Solution**: Create bidirectional mapping between `TypeSchema` and `Type` structures

### 2. Grammar Integration
**Challenge**: Ensure schema syntax is properly supported in the main grammar
**Solution**: Validate that `tree-sitter-lambda/grammar.js` supports all required schema constructs

### 3. Memory Management
**Challenge**: Coordinate memory pools between schema parser and AST builder
**Solution**: Use the transpiler's existing `ast_pool` for all schema-related allocations

### 4. Error Handling
**Challenge**: Integrate schema validation errors with existing transpiler error system
**Solution**: Extend the transpiler's error reporting to include schema validation errors

## File Structure Changes

### New Files
- `lambda/schema_ast.hpp` - Unified schema and AST type definitions
- `lambda/schema_builder.cpp` - Integrated schema-aware type builders
- `lambda/type_registry.cpp` - Type definition registry and resolution

### Modified Files
- `lambda/build_ast.cpp` - Enhanced with schema support
- `lambda/ast.hpp` - Extended AST node types
- `lambda/transpiler.hpp` - Schema-aware transpiler
- `lambda/validator/validator.hpp` - Simplified to use unified types

### Deprecated Files
- `lambda/validator/schema_parser.cpp` - Functionality merged into main AST builder

## Benefits

1. **Unified Development**: Single codebase for both AST building and schema validation
2. **Rich Type System**: Full schema support in Lambda scripts
3. **Better Performance**: Eliminate duplicate parsing and validation overhead
4. **Enhanced IDE Support**: Better type inference and validation in development tools
5. **Simplified Architecture**: Remove redundancy between validator and transpiler

## Migration Strategy

1. **Backward Compatibility**: Ensure existing Lambda scripts continue to work
2. **Gradual Migration**: Support both old and new type syntax during transition
3. **Validation Layer**: Keep validator interface for external schema validation
4. **Documentation**: Comprehensive migration guide for users

## Success Metrics

1. All existing tests pass with the new implementation
2. Schema validation performance matches or exceeds current implementation
3. New schema features work correctly in Lambda scripts
4. Memory usage remains within acceptable bounds
5. Integration tests demonstrate end-to-end schema functionality

## Next Steps

1. Review this plan with the development team
2. Create detailed technical specifications for each phase
3. Set up development environment and testing framework
4. Begin Phase 1 implementation

---

*This document will be updated as the implementation progresses and requirements evolve.*
