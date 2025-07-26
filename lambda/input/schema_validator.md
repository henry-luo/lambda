# Lambda Schema Validator Design

This document proposes a structured approach for implementing a validator for Lambda schemas, with specific focus on the doc schema and general applicability to any Lambda schema.

## Overview

The Lambda Schema Validator is designed to validate Lambda data structures against Lambda type definitions, providing comprehensive type checking, structural validation, and semantic analysis.

## Architecture

### Core Components

1. **Schema Parser** - Parses Lambda type definitions
2. **Type System** - Represents and manipulates Lambda types
3. **Validation Engine** - Performs validation logic
4. **Error Reporter** - Provides detailed error messages
5. **Extension System** - Allows custom validation rules

```
┌─────────────────────────────────────────────────────────────┐
│                    Lambda Schema Validator                  │
├─────────────────────────────────────────────────────────────┤
│  Schema Parser  │  Type System   │  Validation Engine       │
│  - Parse types  │  - Type repr   │  - Structure validation  │
│  - Build AST    │  - Type ops    │  - Type checking         │
│  - Resolve refs │  - Unification │  - Constraint validation │
├─────────────────────────────────────────────────────────────┤
│  Error Reporter │  Extension System │  Utility Functions    │
│  - Error msgs   │  - Custom rules   │  - Path tracking      │
│  - Suggestions  │  - Plugins        │  - Context mgmt       │
└─────────────────────────────────────────────────────────────┘
```

## 1. Schema Parser

### Purpose
Parse Lambda type definitions and build an internal representation suitable for validation.

### Key Features
- Parse type statements (`type Name = Type`)
- Parse entity types (`type Name < attrs, content >`)  
- Parse object types (`type Name { fields }`)
- Resolve type references and build dependency graph
- Handle type unions, intersections, and occurrences

### Implementation Structure

```lambda
type SchemaParser = {
    // Parse a Lambda schema file
    parse_schema: (source: string) => ParseResult,
    
    // Parse individual type definition
    parse_type_def: (def: string) => TypeDefinition,
    
    // Resolve type references
    resolve_references: (defs: [TypeDefinition*]) => ResolvedSchema,
    
    // Build validation-ready schema
    build_schema: (resolved: ResolvedSchema) => ValidationSchema
}

type ParseResult = {
    definitions: [TypeDefinition*],
    imports: [Import*],
    errors: [ParseError*]
}

type TypeDefinition = {
    name: string,
    type: TypeExpression,
    location: SourceLocation
}
```

## 2. Type System

### Purpose
Represent Lambda types in a way that supports validation operations like type checking, unification, and subtype relationships.

### Type Representation

```lambda
type Type = 
    | PrimitiveType
    | UnionType  
    | IntersectionType
    | ArrayType
    | MapType
    | ElementType
    | FunctionType
    | ReferenceType
    | OccurrenceType

type PrimitiveType = {
    kind: primitive,
    name: string  // null, bool, int, float, string, etc.
}

type UnionType = {
    kind: union,
    types: [Type*]
}

type IntersectionType = {
    kind: intersection,
    types: [Type*]
}

type ArrayType = {
    kind: array,
    element_type: Type?  // optional for empty arrays
}

type MapType = {
    kind: map,
    fields: {string: Type},
    open: bool  // allows additional fields
}

type ElementType = {
    kind: element,
    tag: string,
    attributes: {string: Type},
    content: [Type*]?,
    open: bool
}

type OccurrenceType = {
    kind: occurrence,
    base_type: Type,
    modifier: '?' | '+' | '*'
}
```

### Type Operations

```lambda
type TypeSystem = {
    // Check if value matches type
    matches: (value: any, type: Type) => ValidationResult,
    
    // Check if type1 is subtype of type2
    is_subtype: (type1: Type, type2: Type) => bool,
    
    // Unify two types (find common supertype)
    unify: (type1: Type, type2: Type) => Type?,
    
    // Normalize type (resolve unions, simplify)
    normalize: (type: Type) => Type,
    
    // Get all possible types for a union
    expand_union: (type: Type) => [Type*]
}
```

## 3. Validation Engine

### Purpose
Core validation logic that checks Lambda data against type definitions.

### Validation Phases

1. **Structural Validation** - Check basic structure (arrays, maps, elements)
2. **Type Validation** - Check value types match expected types  
3. **Constraint Validation** - Check additional constraints (ranges, patterns)
4. **Semantic Validation** - Check domain-specific rules

### Implementation

```lambda
type ValidationEngine = {
    // Validate a complete document
    validate_document: (data: any, schema: ValidationSchema) => ValidationResult,
    
    // Validate specific value against type
    validate_value: (value: any, type: Type, context: ValidationContext) => ValidationResult,
    
    // Validate element structure
    validate_element: (element: Element, type: ElementType, context: ValidationContext) => ValidationResult,
    
    // Validate map/object structure  
    validate_map: (map: Map, type: MapType, context: ValidationContext) => ValidationResult,
    
    // Validate array structure
    validate_array: (array: Array, type: ArrayType, context: ValidationContext) => ValidationResult
}

type ValidationResult = {
    valid: bool,
    errors: [ValidationError*],
    warnings: [ValidationWarning*]
}

type ValidationContext = {
    path: [PathSegment*],  // Current path in data structure
    schema: ValidationSchema,
    options: ValidationOptions,
    custom_validators: {string: CustomValidator}
}

type PathSegment = 
    | {kind: field, name: string}
    | {kind: index, index: int}
    | {kind: element, tag: string}
    | {kind: attribute, name: string}
```

## 4. Error Reporting

### Purpose
Provide clear, actionable error messages with context and suggestions.

### Error Types

```lambda
type ValidationError = {
    code: ErrorCode,
    message: string,
    path: [PathSegment*],
    location: SourceLocation?,
    expected: Type?,
    actual: any?,
    suggestions: [string*]?
}

type ErrorCode = 
    | type_mismatch
    | missing_required_field
    | unexpected_field  
    | invalid_element_structure
    | constraint_violation
    | reference_error
    | occurrence_error
```

### Error Message Examples

```
Error: Type mismatch at path document.meta.author[0].name
  Expected: string (MetaInlines)  
  Actual: number (42)
  Suggestion: Convert number to string or check data source

Error: Missing required field at path document.body
  Expected: BlockContent* 
  Missing: body element
  Suggestion: Add <body> element to document

Error: Invalid element structure at path document.body.p[0]
  Expected: <p id?, class?, data-*, InlineContent*>
  Actual: <p "content" unexpected_attr:value>
  Suggestion: Remove unexpected_attr or check element type definition
```

## 5. Doc Schema Specific Validators

### Custom Validation Rules for Doc Schema

```lambda
type DocSchemaValidator = {
    // Validate citation references exist
    validate_citations: (doc: Document) => ValidationResult,
    
    // Validate header hierarchy (h1, h2, h3 sequence)
    validate_header_hierarchy: (body: Body) => ValidationResult,
    
    // Validate table structure consistency
    validate_table_consistency: (table: Table) => ValidationResult,
    
    // Validate metadata completeness for publication
    validate_publication_metadata: (meta: Meta) => ValidationResult,
    
    // Validate cross-references and links
    validate_references: (doc: Document) => ValidationResult
}
```

### Citation Validation Example

```lambda
fn validate_citations(doc: Document): ValidationResult {
    let citations = find_all_citations(doc.body)
    let references = doc.meta?.references or []
    let reference_ids = set(references.map(r => r.id))
    
    let errors = []
    for citation in citations {
        if citation.id not in reference_ids {
            errors.push({
                code: reference_error,
                message: format("Citation '{}' not found in references", citation.id),
                path: get_citation_path(citation),
                suggestions: suggest_similar_references(citation.id, reference_ids)
            })
        }
    }
    
    {valid: len(errors) == 0, errors: errors, warnings: []}
}
```

## 6. Configuration and Extensibility

### Validation Options

```lambda
type ValidationOptions = {
    // Strictness level
    strict: bool,           // Fail on warnings
    allow_unknown_fields: bool,
    allow_empty_elements: bool,
    
    // Format-specific options
    html_compatibility: bool,  // Check HTML5 compatibility
    pandoc_compatibility: bool, // Check Pandoc AST compatibility
    
    // Performance options
    max_depth: int?,
    timeout_ms: int?,
    
    // Custom rules
    enabled_rules: [string*]?,
    disabled_rules: [string*]?
}
```

### Custom Validators

```lambda
type CustomValidator = {
    name: string,
    description: string,
    validate: (value: any, type: Type, context: ValidationContext) => ValidationResult
}

// Example: URL validation
let url_validator: CustomValidator = {
    name: "url_validator",
    description: "Validates URL format and accessibility",
    validate: fn(value, type, context) {
        if type.name == "string" and context.path.last?.name == "href" {
            if not is_valid_url(value) {
                return {
                    valid: false,
                    errors: [{
                        code: constraint_violation,
                        message: format("Invalid URL format: {}", value),
                        path: context.path
                    }],
                    warnings: []
                }
            }
        }
        {valid: true, errors: [], warnings: []}
    }
}
```

## 7. Usage Examples

### Basic Validation

```lambda
import doc_schema
import schema_validator

let validator = schema_validator.create({
    schema: doc_schema,
    options: {strict: true, html_compatibility: true}
})

let document = parse_document("sample.mark")
let result = validator.validate_document(document, "doc")

if not result.valid {
    for error in result.errors {
        print("Error: {} at {}", error.message, format_path(error.path))
    }
}
```

### Custom Rule Validation

```lambda
let custom_validator = schema_validator.create({
    schema: doc_schema,
    custom_validators: [url_validator, image_validator],
    options: {
        enabled_rules: ["validate_citations", "validate_header_hierarchy"],
        strict: false
    }
})

let result = custom_validator.validate_document(document, "doc")
```

## 8. Implementation Phases

### Phase 1: Core Infrastructure
- [ ] Lambda type parser
- [ ] Basic type system representation  
- [ ] Simple validation engine for primitives and arrays
- [ ] Basic error reporting

### Phase 2: Advanced Types
- [ ] Element type validation
- [ ] Map type validation with open/closed semantics
- [ ] Union and intersection type support
- [ ] Occurrence modifiers (?, +, *)

### Phase 3: Doc Schema Integration
- [ ] Doc schema specific validators
- [ ] Citation validation
- [ ] Cross-reference checking
- [ ] Metadata validation

### Phase 4: Tooling and Performance
- [ ] CLI validator tool
- [ ] IDE integration (VS Code extension)
- [ ] Performance optimization
- [ ] Batch validation support

### Phase 5: Advanced Features
- [ ] Schema evolution and compatibility checking
- [ ] Auto-repair suggestions
- [ ] Format conversion validation
- [ ] Custom rule DSL

## 9. Benefits

### For Doc Schema
- **Quality Assurance**: Catch structural and semantic errors early
- **Format Consistency**: Ensure documents conform to expected structure
- **Citation Integrity**: Validate all citations have corresponding references
- **Metadata Completeness**: Check required fields for publication

### For General Lambda Schemas
- **Type Safety**: Catch type mismatches at validation time
- **Schema Evolution**: Track breaking changes between schema versions
- **Documentation**: Schema serves as living documentation
- **Tooling Support**: Enable IDE features like autocomplete and error highlighting

## 10. Future Extensions

- **Schema Inference**: Automatically infer schemas from example data
- **Schema Diff**: Compare schemas and highlight differences  
- **Performance Profiling**: Identify validation bottlenecks
- **Web Assembly**: Compile validator for browser use
- **Language Bindings**: Support validation from other languages

This design provides a solid foundation for implementing a comprehensive Lambda schema validator that can grow with the ecosystem while maintaining high performance and usability.
