# Lambda AST-Based Validator Implementation Plan

## Overview
Replace the existing schema-based validator with a new validator that uses the native Lambda AST built by `build_ast.cpp`. This eliminates duplicate type parsing logic and provides better integration with the Lambda type system.

## Phase 1: Basic Infrastructure (Week 1)
**Goal:** Create minimal validator that can validate primitive types using Lambda AST

### 1.1 Core Structures
- Create `AstValidator` struct using `Transpiler` directly
- Implement `ast_validator_create()` and `ast_validator_destroy()`
- Add AST-based validation context and error reporting
- Use existing `ValidationResult` and `ValidationError` structures

### 1.2 AST Integration
- Implement `ast_validator_load_schema()` using `transpiler_build_ast()`
- Create `extract_type_from_ast_node()` to get `Type*` from `AstNode*`
- Add type registry using native Lambda types instead of custom schemas

### 1.3 Primitive Validation
- Implement `validate_against_primitive_type()` for basic types
- Support: string, int, float, bool, null validation
- Create unit tests for primitive type validation

**Deliverables:**
- `./lambda/validator.cpp` - New AST-based validator
- `./lambda/validator.h` - Header with new API
- `./test/test_ast_validator.cpp` - Unit tests for Phase 1

## Phase 2: Composite Types (Week 2)
**Goal:** Add support for arrays, maps, and lists

### 2.1 Array Validation
- Implement `validate_against_array_type()` using `TypeArray*`
- Support element type validation and occurrence constraints
- Handle both `LMD_TYPE_ARRAY` and `LMD_TYPE_LIST`

### 2.2 Map Validation
- Implement `validate_against_map_type()` using `TypeMap*`
- Support key-value type validation
- Handle field requirements and optional fields

### 2.3 Enhanced Testing
- Add comprehensive tests for array and map validation
- Test nested structures and complex type combinations
- Performance benchmarking against existing validator

**Deliverables:**
- Extended validator with array/map support
- Comprehensive test suite for composite types
- Performance comparison report

## Phase 3: Element Validation (Week 3)
**Goal:** Support Lambda element validation using `TypeElmt*`

### 3.1 Element Structure Validation
- Implement `validate_against_element_type()` using `TypeElmt*`
- Support attribute validation and child element validation
- Handle element occurrence patterns (`?`, `*`, `+`)

### 3.2 Content Validation
- Support mixed content validation (text + elements)
- Validate element hierarchies and nesting rules
- Handle element name matching and namespace support

### 3.3 Integration Testing
- Test with real Lambda schema files
- Validate against existing test cases
- Ensure compatibility with current validation expectations

**Deliverables:**
- Full element validation support
- Integration with existing Lambda schema files
- Regression test suite

## Phase 4: Advanced Features (Week 4)
**Goal:** Complete feature parity with existing validator

### 4.1 Union and Reference Types
- Implement union type validation (multiple valid types)
- Support type references and recursive type definitions
- Handle forward references and circular dependencies

### 4.2 Constraints and Custom Validation
- Support occurrence constraints (`min`, `max`, `exactly`)
- Add custom validation hooks
- Implement validation options (strict mode, unknown fields)

### 4.3 Error Reporting Enhancement
- Improve error messages with AST context
- Add source location information from AST nodes
- Create detailed validation reports

**Deliverables:**
- Complete AST-based validator with full feature parity
- Enhanced error reporting system
- Migration guide from old validator

## Technical Implementation Details

### Core Architecture
```cpp
typedef struct AstValidator {
    Transpiler* transpiler;           // Direct use of transpiler
    VariableMemPool* pool;           // Shared memory pool
    HashMap* type_definitions;       // Native Type* registry
    ValidationOptions options;       // Validation configuration
} AstValidator;
```

### Key Functions
```cpp
// Core API
AstValidator* ast_validator_create(VariableMemPool* pool);
int ast_validator_load_schema(AstValidator* validator, const char* source, const char* root_type);
ValidationResult* ast_validator_validate(AstValidator* validator, ConstItem item, const char* type_name);

// AST Integration
Type* extract_type_from_ast_node(AstNode* node);
ValidationResult* validate_against_type(AstValidator* validator, ConstItem item, Type* type);

// Type-specific validation
ValidationResult* validate_against_primitive_type(ConstItem item, Type* type);
ValidationResult* validate_against_array_type(AstValidator* validator, ConstItem item, TypeArray* type);
ValidationResult* validate_against_map_type(AstValidator* validator, ConstItem item, TypeMap* type);
ValidationResult* validate_against_element_type(AstValidator* validator, ConstItem item, TypeElmt* type);
```

### Memory Management
- Use single `ast_pool` from transpiler for all allocations
- Leverage existing Lambda memory management patterns
- Avoid duplicate memory pools and complex cleanup

### Integration Points
- Replace `schema_parser.cpp` usage with direct `transpiler_build_ast()` calls
- Use existing `build_occurrence_type()`, `build_map()`, `build_elmt()` functions
- Maintain compatibility with current `ValidationResult` API

## Testing Strategy

### Unit Tests (Each Phase)
- Test individual validation functions in isolation
- Mock AST structures for controlled testing
- Verify error handling and edge cases

### Integration Tests
- Test with real Lambda schema files
- Validate against existing validator test cases
- Performance comparison benchmarks

### Regression Tests
- Ensure no functionality loss during migration
- Test all existing validation scenarios
- Verify error message compatibility

## Success Criteria

1. **Functionality:** Complete feature parity with existing validator
2. **Performance:** Equal or better validation performance
3. **Memory:** Reduced memory usage through unified pool management
4. **Maintainability:** Simplified codebase with eliminated duplication
5. **Integration:** Seamless replacement of existing validator

## Migration Path

1. Implement new validator alongside existing one
2. Add feature flag to switch between validators
3. Run parallel validation during testing phase
4. Gradual migration of test cases to new validator
5. Remove old validator once full compatibility achieved

## Risk Mitigation

- **AST Compatibility:** Extensive testing with existing schemas
- **Performance Regression:** Continuous benchmarking during development
- **Memory Issues:** Careful memory pool management and leak testing
- **Integration Problems:** Incremental integration with rollback capability
