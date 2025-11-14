# Lambda Validator Guide

## Overview

The Lambda validator provides comprehensive schema-based validation for data documents in Lambda scripts. It supports multiple input formats (XML, HTML, JSON), flexible validation options, and detailed error reporting.

## Schema Definition

### Basic Types

Lambda schemas use a concise, expressive syntax:

```lambda
// primitive types
type Name = string
type Age = int
type Score = float
type Active = bool

// optional types (can be null)
type OptionalEmail = string?
type OptionalAge = int?

// arrays
type Names = [string]
type Numbers = [int]
type OptionalList = [string]?  // array itself is optional

// maps
type Person = {
    name: string,
    age: int,
    email: string?
}
```

### Array Occurrence Operators

Control array cardinality with occurrence operators:

```lambda
type Document = {
    // exactly one array (required)
    tags: [string],

    // zero or more elements (can be empty)
    comments: [string*],

    // one or more elements (must have at least one)
    authors: [string+],

    // optional array (can be missing, but if present must have elements)
    attachments: [string]?
}
```

### Nested Structures

Define complex nested types:

```lambda
type Address = {
    street: string,
    city: string,
    zipcode: string
}

type Company = {
    name: string,
    address: Address
}

type Person = {
    name: string,
    age: int,
    company: Company?  // optional nested structure
}

// recursive types for tree structures
type Node = {
    value: int,
    children: [Node*]  // zero or more child nodes
}
```

### Document Schemas

For XML/HTML validation, use element schemas:

```lambda
// XML article schema
type Article = <article
    title: string,
    author: string,
    date: string?,
    tags: [string*];
    <content>
    <metadata id: string, status: string;>?
>

// HTML page schema
type Page = <html;
    <head;
        <title>
        <meta name: string, content: string;>*
    >
    <body;
        <h1>+
        <p>*
        <div class: string?;>*
    >
>
```

## Usage in Lambda Scripts

### Basic Validation

```lambda
// load schema
let schema = load_schema("schema.lmd", "Person")

// validate data
let person_data = {name: "Alice", age: 30}
let result = validate(schema, person_data)

if (result.valid) {
    print("Validation passed")
} else {
    print("Validation failed:")
    for (error in result.errors) {
        print("  - " + error.message + " at " + error.path)
    }
}
```

### Format-Aware Validation

The validator automatically detects input formats and unwraps document wrappers:

```lambda
// XML validation with auto-detection
let xml_data = input("article.xml", 'xml')
let result = validate_with_format(schema, xml_data, 'xml')

// HTML validation
let html_data = input("page.html", 'html')
let result = validate_with_format(schema, html_data, 'html')

// JSON validation
let json_data = input("data.json", 'json')
let result = validate_with_format(schema, json_data, 'json')

// auto-detect format (validator inspects structure)
let data = input("document", 'auto')
let result = validate_with_format(schema, data, null)
```

### Validation Options

Configure validation behavior with options:

```lambda
// strict mode: all optional fields must be explicitly null or present
let strict_result = validate_strict(schema, data)

// limit maximum errors reported
let result = validate_with_options(schema, data, {
    max_errors: 5,
    strict_mode: true
})

// limit validation depth for nested structures
let result = validate_with_options(schema, data, {
    max_depth: 10
})

// allow unknown fields (not in schema)
let result = validate_with_options(schema, data, {
    allow_unknown_fields: true
})
```

## Error Handling

### Error Structure

Validation errors provide detailed information:

```lambda
type ValidationError = {
    path: string,        // JSON path to error location (e.g., "person.address.city")
    message: string,     // human-readable error description
    expected: string,    // expected type or value
    actual: string       // actual type or value found
}

type ValidationResult = {
    valid: bool,
    errors: [ValidationError*]
}
```

### Common Error Patterns

```lambda
// handle type mismatches
let result = validate(schema, data)
if (!result.valid) {
    for (error in result.errors) {
        if (contains(error.message, "type mismatch")) {
            print("Type error at " + error.path + ": expected " +
                  error.expected + ", got " + error.actual)
        }
    }
}

// handle missing required fields
let result = validate(schema, data)
if (!result.valid) {
    let missing = filter(result.errors, fn(e) contains(e.message, "required"))
    if (length(missing) > 0) {
        print("Missing required fields:")
        for (error in missing) {
            print("  - " + error.path)
        }
    }
}

// handle array occurrence violations
let result = validate(schema, data)
if (!result.valid) {
    for (error in result.errors) {
        if (contains(error.message, "one or more")) {
            print("Array at " + error.path + " must have at least one element")
        }
    }
}
```

## Format-Specific Features

### XML Document Unwrapping

The validator automatically unwraps XML `<document>` wrappers:

```lambda
// input XML with document wrapper
// <document>
//   <article>
//     <title>Hello</title>
//   </article>
// </document>

let xml = input("article.xml", 'xml')
let result = validate_with_format(schema, xml, 'xml')
// validator automatically unwraps <document> and validates <article>
```

### HTML Body Extraction

HTML validation extracts the `<body>` element:

```lambda
// input HTML
// <html>
//   <head><title>Page</title></head>
//   <body>
//     <h1>Header</h1>
//     <p>Content</p>
//   </body>
// </html>

let html = input("page.html", 'html')
let result = validate_with_format(schema, html, 'html')
// validator extracts <body> and validates its contents
```

### JSON Structure Validation

JSON validation works on maps and arrays:

```lambda
// validate JSON object
let json_obj = {name: "Alice", age: 30}
let result = validate(schema, json_obj)

// validate JSON array
let json_arr = [1, 2, 3, 4, 5]
let result = validate(array_schema, json_arr)
```

## Best Practices

### 1. Schema Organization

Organize schemas into reusable types:

```lambda
// common types
type EmailAddress = string  // can add validation later
type PhoneNumber = string
type Timestamp = string

// domain types
type User = {
    id: int,
    email: EmailAddress,
    phone: PhoneNumber?,
    created: Timestamp
}

type Post = {
    id: int,
    author: User,
    title: string,
    content: string,
    published: Timestamp?
}
```

### 2. Optional vs Null

Use optional types for fields that may be omitted:

```lambda
// good: optional email field
type Person = {
    name: string,
    age: int,
    email: string?  // can be missing or null
}

// validate with null (explicit null value)
let person1 = {name: "Alice", age: 30, email: null}
validate(schema, person1)  // valid

// validate with missing field
let person2 = {name: "Bob", age: 25}
validate(schema, person2)  // valid

// use strict mode to require explicit null
let result = validate_with_options(schema, person2, {strict_mode: true})
// invalid - email field must be present (even if null)
```

### 3. Array Constraints

Choose appropriate array occurrence operators:

```lambda
// use * for optional lists (can be empty)
type Article = {
    tags: [string*]  // zero or more tags
}

// use + for required lists (must have elements)
type Team = {
    members: [string+]  // at least one member required
}

// use ? for optional arrays (can be missing entirely)
type Document = {
    attachments: [string]?  // array can be missing
}
```

### 4. Error Handling Strategy

Always handle validation errors gracefully:

```lambda
fn validate_and_process(data) {
    let result = validate(schema, data)

    if (!result.valid) {
        // log errors
        log("Validation failed:", result.errors)

        // early return or default handling
        return {success: false, errors: result.errors}
    }

    // process valid data
    return {success: true, data: process(data)}
}
```

### 5. Format Detection

Let the validator auto-detect format when possible:

```lambda
// good: let validator detect format
let data = input("document", 'auto')
let result = validate_with_format(schema, data, null)

// also good: explicit format when known
let xml_data = input("article.xml", 'xml')
let result = validate_with_format(schema, xml_data, 'xml')
```

### 6. Performance Considerations

For large or deeply nested documents:

```lambda
// limit validation depth
let result = validate_with_options(schema, data, {
    max_depth: 20  // prevent excessive recursion
})

// limit error reporting (stop after N errors)
let result = validate_with_options(schema, data, {
    max_errors: 10  // faster for documents with many errors
})
```

## Advanced Patterns

### Union Types (Future)

*Note: Union types are planned for future releases*

```lambda
// future syntax for union types
type StringOrNumber = string | int
type Result = {success: true, data: any} | {success: false, error: string}
```

### Type Constraints (Future)

*Note: Constraint validation is planned for future releases*

```lambda
// future syntax for constraints
type Age = int where (value >= 0 and value <= 150)
type Email = string where matches(value, /^[^\s@]+@[^\s@]+\.[^\s@]+$/)
```

### Schema Composition

Compose schemas from reusable parts:

```lambda
// base types
type Identifiable = {
    id: int,
    created: string
}

type Timestamped = {
    created: string,
    modified: string?
}

// composed types (manually merged for now)
type User = {
    id: int,
    created: string,
    modified: string?,
    name: string,
    email: string
}
```

## API Reference

### Core Functions

```lambda
// load schema from file
load_schema(path: string, type_name: string) -> Schema

// validate with basic schema
validate(schema: Schema, data: Item) -> ValidationResult

// validate with format awareness
validate_with_format(schema: Schema, data: Item, format: string?) -> ValidationResult

// validate with options
validate_with_options(schema: Schema, data: Item, options: {
    strict_mode: bool?,
    max_errors: int?,
    max_depth: int?,
    allow_unknown_fields: bool?
}) -> ValidationResult

// validate with strict mode (shorthand)
validate_strict(schema: Schema, data: Item) -> ValidationResult
```

### Format Constants

```lambda
'xml'   // XML format with document unwrapping
'html'  // HTML format with body extraction
'json'  // JSON format (maps and arrays)
null    // auto-detect format
```

### Default Options

```lambda
{
    strict_mode: false,           // allow missing optional fields
    max_errors: 100,              // report up to 100 errors
    max_depth: 100,               // validate up to 100 levels deep
    allow_unknown_fields: false   // reject unknown fields
}
```

## Examples

See `examples/validation/` for complete working examples:
- `examples/validation/basic.lmd` - Basic validation
- `examples/validation/xml_article.lmd` - XML document validation
- `examples/validation/nested_types.lmd` - Complex nested structures
- `examples/validation/error_handling.lmd` - Error handling patterns
- `examples/validation/options.lmd` - Validation options

## Troubleshooting

### Common Issues

**Issue**: "Unknown type" error
**Solution**: Ensure type is defined before use. Use `load_schema()` to load type definitions.

**Issue**: Validation passes but data seems invalid
**Solution**: Check if `allow_unknown_fields: true` is set. Disable for strict validation.

**Issue**: "Maximum depth exceeded" error
**Solution**: Increase `max_depth` option or check for circular references in data.

**Issue**: XML validation fails on wrapped documents
**Solution**: Use `validate_with_format()` with `'xml'` format to auto-unwrap.

**Issue**: Too many validation errors
**Solution**: Set `max_errors` to stop early and inspect first few errors.

## Further Reading

- [Lambda Reference Manual](Lambda_Reference.md) - Complete language reference
- [Lambda Cheatsheet](Lambda_Cheatsheet.md) - Quick syntax reference
- [Type System Documentation](Lambda_Types.md) - Detailed type system guide
