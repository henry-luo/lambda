// Lambda Schema Validator Implementation
// Core types and functions for validating Lambda data against Lambda schemas

// ==================== Core Type System ====================

// Type representation for validation
type Type = 
    | PrimitiveType { name: string }
    | UnionType { types: [Type*] }
    | IntersectionType { types: [Type*] }
    | ArrayType { element_type: Type? }
    | MapType { fields: {string: Type}, open: bool }
    | ElementType { 
        tag: string, 
        attributes: {string: Type}, 
        content: [Type*]?, 
        open: bool 
    }
    | FunctionType { params: [Type*], return_type: Type }
    | ReferenceType { name: string }
    | OccurrenceType { base_type: Type, modifier: symbol }
    | LiteralType { value: any }

// Validation result types
type ValidationResult = {
    valid: bool,
    errors: [ValidationError*],
    warnings: [ValidationWarning*]
}

type ValidationError = {
    code: ErrorCode,
    message: string,
    path: [PathSegment*],
    location: SourceLocation?,
    expected: Type?,
    actual: any?,
    suggestions: [string*]?
}

type ValidationWarning = {
    code: WarningCode,
    message: string,
    path: [PathSegment*],
    suggestion: string?
}

type ErrorCode = 
    | type_mismatch
    | missing_required_field
    | unexpected_field
    | invalid_element_structure
    | constraint_violation
    | reference_error
    | occurrence_error
    | circular_reference

type WarningCode =
    | deprecated_field
    | missing_optional_field
    | style_violation
    | performance_warning

type PathSegment = 
    | FieldSegment { name: string }
    | IndexSegment { index: int }
    | ElementSegment { tag: string }
    | AttributeSegment { name: string }

type SourceLocation = {
    file: string?,
    line: int,
    column: int
}

// Validation context for tracking state
type ValidationContext = {
    path: [PathSegment*],
    schema: ValidationSchema,
    options: ValidationOptions,
    custom_validators: {string: CustomValidator},
    visited: {string: bool}  // for circular reference detection
}

type ValidationOptions = {
    strict: bool,
    allow_unknown_fields: bool,
    allow_empty_elements: bool,
    max_depth: int?,
    timeout_ms: int?,
    enabled_rules: [string*]?,
    disabled_rules: [string*]?
}

type ValidationSchema = {
    types: {string: Type},
    root_type: string,
    imports: [string*],
    metadata: {string: any}
}

type CustomValidator = {
    name: string,
    description: string,
    validate: (value: any, type: Type, context: ValidationContext) => ValidationResult
}

// ==================== Schema Parser ====================

type SchemaParser <
    // Parse Lambda schema from source code
    parse: (source: string) => ParseResult,
    
    // Parse individual type definition  
    parse_type_def: (def: string) => TypeDefinition?,
    
    // Resolve type references and build schema
    build_schema: (definitions: [TypeDefinition*]) => ValidationSchema
>

type ParseResult = {
    definitions: [TypeDefinition*],
    imports: [string*],
    errors: [ParseError*]
}

type TypeDefinition = {
    name: string,
    type_expr: TypeExpression,
    location: SourceLocation
}

type TypeExpression = string  // Raw type expression to be parsed

type ParseError = {
    message: string,
    location: SourceLocation
}

// Example parser implementation functions
pub fn parse_schema(source: string): ParseResult {
    // Parse Lambda type definitions from source
    // This would use the Lambda grammar to parse type statements
    {
        definitions: [],  // Parsed type definitions
        imports: [],      // Import statements
        errors: []        // Parse errors
    }
}

pub fn build_validation_schema(definitions: [TypeDefinition*]): ValidationSchema {
    // Convert parsed definitions to validation-ready schema
    let types = {}
    
    for def in definitions {
        types[def.name] = parse_type_expression(def.type_expr)
    }
    
    {
        types: types,
        root_type: "doc",  // Default root type
        imports: [],
        metadata: {}
    }
}

// ==================== Type System Operations ====================

type TypeSystem <
    // Check if value matches type
    matches: (value: any, type: Type, context: ValidationContext) => ValidationResult,
    
    // Check subtype relationship
    is_subtype: (type1: Type, type2: Type) => bool,
    
    // Unify types (find common supertype)
    unify: (type1: Type, type2: Type) => Type?,
    
    // Normalize type (resolve references, simplify unions)
    normalize: (type: Type, schema: ValidationSchema) => Type
>

pub fn matches(value: any, type: Type, context: ValidationContext): ValidationResult {
    // Dispatch to specific validation functions based on type
    type match {
        PrimitiveType => validate_primitive(value, type, context),
        UnionType => validate_union(value, type, context),
        ArrayType => validate_array(value, type, context),
        MapType => validate_map(value, type, context),
        ElementType => validate_element(value, type, context),
        OccurrenceType => validate_occurrence(value, type, context),
        ReferenceType => validate_reference(value, type, context),
        _ => {valid: false, errors: [{
            code: type_mismatch,
            message: format("Unknown type: {}", type),
            path: context.path
        }], warnings: []}
    }
}

fn validate_primitive(value: any, type: PrimitiveType, context: ValidationContext): ValidationResult {
    let value_type = type(value)
    let expected = type.name
    
    let valid = (expected, value_type) match {
        ("null", "null") => true,
        ("bool", "bool") => true,
        ("int", "int") => true,
        ("float", "float") => true,
        ("number", "int" | "float") => true,
        ("string", "string") => true,
        ("symbol", "symbol") => true,
        ("any", _) => true,
        _ => false
    }
    
    if valid {
        {valid: true, errors: [], warnings: []}
    } else {
        {valid: false, errors: [{
            code: type_mismatch,
            message: format("Expected {}, got {}", expected, value_type),
            path: context.path,
            expected: type,
            actual: value
        }], warnings: []}
    }
}

fn validate_array(value: any, type: ArrayType, context: ValidationContext): ValidationResult {
    if type(value) != "array" {
        return {valid: false, errors: [{
            code: type_mismatch,
            message: format("Expected array, got {}", type(value)),
            path: context.path
        }], warnings: []}
    }
    
    let errors = []
    let warnings = []
    
    // Validate each element if element type is specified
    if type.element_type? {
        for i, element in value {
            let element_context = {
                ...context,
                path: context.path + [IndexSegment{index: i}]
            }
            let result = matches(element, type.element_type, element_context)
            errors = errors + result.errors
            warnings = warnings + result.warnings
        }
    }
    
    {valid: len(errors) == 0, errors: errors, warnings: warnings}
}

fn validate_map(value: any, type: MapType, context: ValidationContext): ValidationResult {
    if type(value) != "map" {
        return {valid: false, errors: [{
            code: type_mismatch,
            message: format("Expected map, got {}", type(value)),
            path: context.path
        }], warnings: []}
    }
    
    let errors = []
    let warnings = []
    
    // Check required fields
    for field_name, field_type in type.fields {
        if field_name in value {
            let field_context = {
                ...context,
                path: context.path + [FieldSegment{name: field_name}]
            }
            let result = matches(value[field_name], field_type, field_context)
            errors = errors + result.errors
            warnings = warnings + result.warnings
        } else if not is_optional_type(field_type) {
            errors = errors + [{
                code: missing_required_field,
                message: format("Missing required field: {}", field_name),
                path: context.path + [FieldSegment{name: field_name}]
            }]
        }
    }
    
    // Check for unexpected fields
    if not type.open {
        for field_name in keys(value) {
            if field_name not in type.fields {
                if context.options.strict {
                    errors = errors + [{
                        code: unexpected_field,
                        message: format("Unexpected field: {}", field_name),
                        path: context.path + [FieldSegment{name: field_name}]
                    }]
                } else {
                    warnings = warnings + [{
                        code: style_violation,
                        message: format("Unknown field: {}", field_name),
                        path: context.path + [FieldSegment{name: field_name}]
                    }]
                }
            }
        }
    }
    
    {valid: len(errors) == 0, errors: errors, warnings: warnings}
}

fn validate_element(value: any, type: ElementType, context: ValidationContext): ValidationResult {
    if type(value) != "element" {
        return {valid: false, errors: [{
            code: type_mismatch,
            message: format("Expected element, got {}", type(value)),
            path: context.path
        }], warnings: []}
    }
    
    let errors = []
    let warnings = []
    
    // Check element tag matches
    if value.tag != type.tag {
        errors = errors + [{
            code: invalid_element_structure,
            message: format("Expected element <{}>, got <{}>", type.tag, value.tag),
            path: context.path
        }]
    }
    
    // Validate attributes
    for attr_name, attr_type in type.attributes {
        if attr_name in value.attrs {
            let attr_context = {
                ...context,
                path: context.path + [ElementSegment{tag: type.tag}, AttributeSegment{name: attr_name}]
            }
            let result = matches(value.attrs[attr_name], attr_type, attr_context)
            errors = errors + result.errors
            warnings = warnings + result.warnings
        } else if not is_optional_type(attr_type) {
            errors = errors + [{
                code: missing_required_field,
                message: format("Missing required attribute: {}", attr_name),
                path: context.path + [ElementSegment{tag: type.tag}]
            }]
        }
    }
    
    // Validate content if specified
    if type.content? {
        let content_context = {
            ...context,
            path: context.path + [ElementSegment{tag: type.tag}]
        }
        
        for i, content_type in type.content {
            if i < len(value.content) {
                let result = matches(value.content[i], content_type, {
                    ...content_context,
                    path: content_context.path + [IndexSegment{index: i}]
                })
                errors = errors + result.errors
                warnings = warnings + result.warnings
            }
        }
    }
    
    {valid: len(errors) == 0, errors: errors, warnings: warnings}
}

fn validate_occurrence(value: any, type: OccurrenceType, context: ValidationContext): ValidationResult {
    type.modifier match {
        '?' => {
            // Optional: value can be null or match base type
            if value == null {
                {valid: true, errors: [], warnings: []}
            } else {
                matches(value, type.base_type, context)
            }
        },
        '+' => {
            // One or more: must be array with at least one element
            if type(value) != "array" or len(value) == 0 {
                {valid: false, errors: [{
                    code: occurrence_error,
                    message: "Expected one or more values (non-empty array)",
                    path: context.path
                }], warnings: []}
            } else {
                validate_array(value, ArrayType{element_type: type.base_type}, context)
            }
        },
        '*' => {
            // Zero or more: must be array (can be empty)
            if type(value) != "array" {
                {valid: false, errors: [{
                    code: occurrence_error,
                    message: "Expected zero or more values (array)",
                    path: context.path
                }], warnings: []}
            } else {
                validate_array(value, ArrayType{element_type: type.base_type}, context)
            }
        }
    }
}

fn validate_union(value: any, type: UnionType, context: ValidationContext): ValidationResult {
    // Value is valid if it matches any type in the union
    for union_type in type.types {
        let result = matches(value, union_type, context)
        if result.valid {
            return result
        }
    }
    
    // If no types match, return error with all attempted types
    {valid: false, errors: [{
        code: type_mismatch,
        message: format("Value does not match any type in union: {}", 
                       type.types.map(t => format_type(t)).join(" | ")),
        path: context.path,
        expected: type,
        actual: value
    }], warnings: []}
}

// ==================== Main Validator Interface ====================

type Validator <
    // Validate complete document against schema
    validate_document: (data: any, schema: ValidationSchema) => ValidationResult,
    
    // Validate value against specific type
    validate_value: (value: any, type_name: string, schema: ValidationSchema) => ValidationResult,
    
    // Add custom validator
    add_validator: (validator: CustomValidator) => Validator,
    
    // Set validation options
    with_options: (options: ValidationOptions) => Validator
>

pub fn create_validator(schema: ValidationSchema, options: ValidationOptions): Validator {
    let context = ValidationContext{
        path: [],
        schema: schema,
        options: options,
        custom_validators: {},
        visited: {}
    }
    
    Validator{
        validate_document: fn(data, schema) {
            validate_value(data, schema.root_type, schema)
        },
        
        validate_value: fn(value, type_name, schema) {
            if type_name not in schema.types {
                return {valid: false, errors: [{
                    code: reference_error,
                    message: format("Unknown type: {}", type_name),
                    path: []
                }], warnings: []}
            }
            
            let type_def = schema.types[type_name]
            matches(value, type_def, context)
        },
        
        add_validator: fn(validator) {
            let new_context = {
                ...context,
                custom_validators: context.custom_validators + {validator.name: validator}
            }
            create_validator(schema, options)  // Return new validator with updated context
        },
        
        with_options: fn(new_options) {
            create_validator(schema, new_options)
        }
    }
}

// ==================== Utility Functions ====================

fn is_optional_type(type: Type): bool {
    type match {
        OccurrenceType{modifier: '?'} => true,
        UnionType{types} => types.any(t => t == PrimitiveType{name: "null"}),
        _ => false
    }
}

fn format_type(type: Type): string {
    type match {
        PrimitiveType{name} => name,
        UnionType{types} => types.map(format_type).join(" | "),
        ArrayType{element_type} => format("[{}*]", 
            element_type?.let(t => format_type(t)) or "any"),
        MapType{fields} => format("{{{}}}", 
            fields.map((k, v) => format("{}: {}", k, format_type(v))).join(", ")),
        ElementType{tag} => format("<{}>", tag),
        OccurrenceType{base_type, modifier} => format("{}{}", format_type(base_type), modifier),
        _ => "unknown"
    }
}

fn format_path(path: [PathSegment*]): string {
    path.map(segment => segment match {
        FieldSegment{name} => format(".{}", name),
        IndexSegment{index} => format("[{}]", index),
        ElementSegment{tag} => format("<{}>", tag),
        AttributeSegment{name} => format("@{}", name)
    }).join("")
}

// ==================== Doc Schema Specific Validators ====================

// Citation validator for doc schema
pub let citation_validator: CustomValidator = {
    name: "citation_validator",
    description: "Validates citation references exist in metadata",
    validate: fn(value, type, context) {
        if type match ElementType{tag: "citation"} {
            let citation_id = value.attrs?.id
            if citation_id? {
                // Look up document root to find references
                let doc = get_document_root(context)
                let references = doc?.meta?.references or []
                let reference_ids = set(references.map(r => r.id))
                
                if citation_id not in reference_ids {
                    return {valid: false, errors: [{
                        code: reference_error,
                        message: format("Citation '{}' not found in references", citation_id),
                        path: context.path,
                        suggestions: suggest_similar_ids(citation_id, reference_ids)
                    }], warnings: []}
                }
            }
        }
        {valid: true, errors: [], warnings: []}
    }
}

// Header hierarchy validator
pub let header_validator: CustomValidator = {
    name: "header_validator", 
    description: "Validates header level hierarchy",
    validate: fn(value, type, context) {
        if type match ElementType{tag} and tag in ["h1", "h2", "h3", "h4", "h5", "h6"] {
            let level = int(tag[1:])
            let parent_headers = get_parent_headers(context)
            
            if len(parent_headers) > 0 {
                let last_level = parent_headers.last.level
                if level > last_level + 1 {
                    return {valid: false, errors: [], warnings: [{
                        code: style_violation,
                        message: format("Header level {} follows level {} (consider h{})", 
                                       level, last_level, last_level + 1),
                        path: context.path
                    }]}
                }
            }
        }
        {valid: true, errors: [], warnings: []}
    }
}

// ==================== Usage Example ====================

pub fn example_usage() {
    // Load doc schema
    let schema_source = read_file("doc_schema.ls")
    let parse_result = parse_schema(schema_source)
    
    if len(parse_result.errors) > 0 {
        print("Schema parse errors:")
        for error in parse_result.errors {
            print("  {}", error.message)
        }
        return
    }
    
    let schema = build_validation_schema(parse_result.definitions)
    
    // Create validator with custom rules
    let validator = create_validator(schema, {
        strict: true,
        allow_unknown_fields: false
    }).add_validator(citation_validator)
      .add_validator(header_validator)
    
    // Load and validate document
    let document = parse_document("sample.mark")
    let result = validator.validate_document(document, schema)
    
    if result.valid {
        print("Document is valid!")
    } else {
        print("Validation errors:")
        for error in result.errors {
            print("  {} at {}: {}", error.code, format_path(error.path), error.message)
            if error.suggestions? {
                for suggestion in error.suggestions {
                    print("    Suggestion: {}", suggestion)
                }
            }
        }
    }
    
    if len(result.warnings) > 0 {
        print("Warnings:")
        for warning in result.warnings {
            print("  {}: {}", format_path(warning.path), warning.message)
        }
    }
}
