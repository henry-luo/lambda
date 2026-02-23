# Lambda Validator Enhancement Proposal: JSON Schema Parity

## Executive Summary

This proposal outlines enhancements to Lambda's schema validation system to achieve feature parity with JSON Schema (draft 2020-12). The goal is to support all JSON Schema validation keywords while maintaining Lambda's clean, functional syntax.

## Current State

### Lambda Type System (Supported)

| Feature | Lambda Syntax | Example |
|---------|---------------|---------|
| Primitive types | `int`, `string`, `bool`, `float`, `null` | `age: int` |
| Union types | `T \| U` | `id: int \| string` |
| Intersection types | `T & U` | `data: Serializable & Comparable` |
| Optional | `T?` | `email: string?` |
| Zero or more | `T*` | `tags: string*` |
| One or more | `T+` | `items: Item+` |
| Exact count | `T[n]` | `coords: float[3]` |
| Range count | `T[min, max]` | `digits: int[4, 6]` |
| Min count | `T[n+]` | `votes: int[1+]` |
| Typed arrays | `T[]` | `scores: int[]` |
| Typed maps | `{key: T}` | `{name: string, age: int}` |
| Elements | `<tag>content</tag>` | `<div class: string>Block*</div>` |
| Type references | `TypeName` | `author: Person` |

### JSON Schema Features (Not Yet Supported)

| Category           | JSON Schema Keywords                                                                           |
| ------------------ | ---------------------------------------------------------------------------------------------- |
| String constraints | `minLength`, `maxLength`, `pattern`, `format`                                                  |
| Number constraints | `minimum`, `maximum`, `exclusiveMinimum`, `exclusiveMaximum`, `multipleOf`                     |
| Array constraints  | `uniqueItems`, `contains`, `minContains`, `maxContains`                                        |
| Object constraints | `additionalProperties`, `propertyNames`, `patternProperties`, `minProperties`, `maxProperties` |
| Conditional        | `if`/`then`/`else`, `not`, `allOf`, `anyOf`, `oneOf`                                           |
| Metadata           | `title`, `description`, `default`, `examples`, `deprecated`, `readOnly`, `writeOnly`           |
| String formats     | `date`, `time`, `date-time`, `email`, `uri`, `uuid`, `ipv4`, `ipv6`, `hostname`, `regex`       |

---

## Proposed Enhancements

### 1. Constraint Expressions

Introduce constraint expressions using `where` clause syntax:

```lambda
// String constraints
type Username = string where {
    minLength: 3,
    maxLength: 20,
    pattern: "^[a-zA-Z][a-zA-Z0-9_]*$"
}

// Number constraints  
type Age = int where {
    minimum: 0,
    maximum: 150
}

type Price = float where {
    minimum: 0,
    exclusiveMaximum: 1000000,
    multipleOf: 0.01
}

// Shorthand for common constraints
type PositiveInt = int where >= 0
type Percentage = float where 0 <= _ <= 100
type NonEmpty = string where len > 0
```

**Implementation Approach:**
- Add `TypeConstrained` AST node with `Type* base_type` and `Constraint* constraints`
- Parse `where` clause after type expression
- Constraints evaluated at validation time

### 2. String Format Validation

Built-in format validators using `@format` annotation:

```lambda
// Built-in formats
type Email = string @format("email")
type DateString = string @format("date")        // ISO 8601 date
type TimeString = string @format("time")        // ISO 8601 time  
type DateTime = string @format("date-time")     // ISO 8601 datetime
type Uri = string @format("uri")
type Uuid = string @format("uuid")
type Ipv4 = string @format("ipv4")
type Ipv6 = string @format("ipv6")
type Hostname = string @format("hostname")
type Regex = string @format("regex")

// Usage in schemas
type User = {
    email: string @format("email"),
    created: string @format("date-time"),
    website: string? @format("uri")
}

// Custom format registration (runtime API)
register_format("phone", pattern: "^\+?[1-9]\d{1,14}$")
type Phone = string @format("phone")
```

**Built-in Formats to Implement:**

| Format | Validation |
|--------|------------|
| `email` | RFC 5322 email address |
| `date` | ISO 8601 date (YYYY-MM-DD) |
| `time` | ISO 8601 time (HH:MM:SS) |
| `date-time` | ISO 8601 datetime |
| `uri` | RFC 3986 URI |
| `uri-reference` | URI or relative reference |
| `uuid` | RFC 4122 UUID |
| `ipv4` | IPv4 address |
| `ipv6` | IPv6 address |
| `hostname` | RFC 1123 hostname |
| `regex` | ECMA-262 regex |
| `json-pointer` | RFC 6901 JSON pointer |

### 3. Array Enhancements

```lambda
// Unique items
type Tags = string[] @unique

// Contains constraint
type Numbers = int[] where contains(int where > 100)

// Min/max contains
type Votes = int[] where {
    contains: int where > 0,
    minContains: 1,
    maxContains: 10
}

// Tuple validation (positional types)
type Point2D = [float, float]
type Point3D = [float, float, float]
type Mixed = [string, int, bool?]  // third element optional

// Tuple with rest
type LogEntry = [string @format("date-time"), string, ...any]
```

### 4. Object/Map Enhancements

```lambda
// Additional properties control
type StrictUser = {
    name: string,
    age: int
} @closed  // No additional properties allowed (additionalProperties: false)

type FlexibleUser = {
    name: string,
    age: int,
    ...: any  // Additional properties of any type
}

type StringExtras = {
    name: string,
    ...: string  // Additional properties must be strings
}

// Property count constraints
type SmallObject = {} where {
    minProperties: 1,
    maxProperties: 5
}

// Property name patterns
type Headers = {
    @pattern("^X-"): string,  // Keys matching pattern
    @pattern("^Content-"): string
}

// Required vs optional (explicit)
type Document = {
    id: string @required,      // Explicit required
    title: string,             // Required by default
    subtitle: string?,         // Optional (existing syntax)
    metadata: {} @optional     // Explicit optional
}
```

### 5. Conditional Validation

```lambda
// If-then-else
type PaymentMethod = {
    type: "credit" | "bank",
    
    // Conditional fields based on type
    @if(type == "credit") {
        cardNumber: string where len == 16,
        cvv: string where len == 3 | len == 4,
        expiry: string @format("date")
    },
    
    @if(type == "bank") {
        accountNumber: string,
        routingNumber: string where len == 9
    }
}

// Dependent required (if field A exists, field B required)
type Form = {
    email: string?,
    emailConfirm: string? @requiredIf(email),
    
    password: string?,
    passwordConfirm: string? @requiredIf(password)
}

// OneOf (exactly one must match)
type Identifier = @oneOf(
    { type: "email", value: string @format("email") },
    { type: "phone", value: string @format("phone") },
    { type: "username", value: string where len >= 3 }
)

// Not (must not match)
type NonAdmin = User where not(role == "admin")
```

### 6. Schema Composition

```lambda
// AllOf (intersection with validation)
type Employee = Person & {
    employeeId: string,
    department: string
} @allOf

// AnyOf (at least one must match)  
type Contact = @anyOf(
    { email: string @format("email") },
    { phone: string },
    { address: Address }
)

// Extending types with additional constraints
type StrictPerson = Person where {
    name: string where len >= 2,
    age: int where >= 18
}
```

### 7. Metadata Annotations

```lambda
// Documentation annotations
type User = {
    @title("User Account")
    @description("Represents a registered user in the system")
    
    id: string @description("Unique identifier") @example("usr_123abc"),
    
    name: string 
        @title("Display Name")
        @description("User's public display name")
        @example("John Doe"),
    
    email: string 
        @format("email")
        @description("Primary email address"),
    
    role: "admin" | "user" | "guest"
        @default("user")
        @description("User's permission level"),
    
    legacyId: int?
        @deprecated("Use 'id' field instead")
        @description("Old numeric identifier"),
    
    internalScore: float
        @readOnly
        @description("Computed engagement score")
}

// Extractable as JSON Schema
// lambda schema export user.ls --format json-schema > user.schema.json
```

### 8. Numeric Type Enhancements

```lambda
// Integer subtypes
type Int8 = int where -128 <= _ <= 127
type Int16 = int where -32768 <= _ <= 32767
type Int32 = int where -2147483648 <= _ <= 2147483647
type UInt8 = int where 0 <= _ <= 255
type UInt16 = int where 0 <= _ <= 65535

// Float constraints
type Latitude = float where -90 <= _ <= 90
type Longitude = float where -180 <= _ <= 180
type Probability = float where 0 <= _ <= 1

// MultipleOf
type EvenInt = int where _ % 2 == 0
type Currency = float where multipleOf(0.01)
type Degrees = float where multipleOf(0.5)
```

### 9. Const and Enum

```lambda
// Const (exact value)
type Version = "1.0" @const
type Protocol = "https" @const

// Enum (explicit allowed values)
type Status = @enum("pending", "active", "suspended", "deleted")
type Priority = @enum(1, 2, 3, 4, 5)

// Enum with descriptions
type HttpMethod = @enum(
    "GET" @description("Retrieve resource"),
    "POST" @description("Create resource"),
    "PUT" @description("Update resource"),
    "DELETE" @description("Remove resource")
)
```

### 10. References and Definitions

```lambda
// Self-reference (recursive types)
type TreeNode = {
    value: any,
    children: TreeNode[]?  // Already supported
}

// Mutual recursion
type Expression = Literal | BinaryOp | UnaryOp
type Literal = { type: "literal", value: int | string | bool }
type BinaryOp = { type: "binary", op: string, left: Expression, right: Expression }
type UnaryOp = { type: "unary", op: string, operand: Expression }

// External schema references
import "common_types.ls" as Common

type User = {
    address: Common.Address,
    phone: Common.PhoneNumber
}

// JSON Schema $ref equivalent
type Order = {
    customer: @ref("#/$defs/Customer"),
    items: @ref("#/$defs/LineItem")[]
}
```

---

## Implementation Phases

### Phase 1: Constraint Expressions (2-3 weeks)

**Scope:**
- Add `where` clause parsing to type expressions
- Implement `TypeConstrained` AST node
- Add constraint evaluation in validator
- Support numeric constraints: `minimum`, `maximum`, `exclusiveMinimum`, `exclusiveMaximum`, `multipleOf`
- Support string constraints: `minLength`, `maxLength`, `pattern`

**Files to Modify:**
- `lambda/tree-sitter-lambda/grammar.js` - Add `where` clause grammar
- `lambda/build_ast.cpp` - Build `TypeConstrained` nodes
- `lambda/validator/validate.cpp` - Evaluate constraints
- `lambda/validator/validate_constraint.cpp` - New file for constraint logic

**Test Cases:**
```lambda
// test/lambda/constraint_basic.ls
type PositiveInt = int where >= 0
assert (5 is PositiveInt) == true
assert (-1 is PositiveInt) == false

type ShortString = string where len <= 10
assert ("hello" is ShortString) == true
assert ("hello world!" is ShortString) == false
```

### Phase 2: Format Validation (1-2 weeks)

**Scope:**
- Implement `@format` annotation parsing
- Add built-in format validators
- Support custom format registration

**Files to Modify:**
- `lambda/build_ast.cpp` - Parse annotations
- `lambda/validator/validate_format.cpp` - New file
- `lib/format_validators.c` - Low-level validators (regex, RFC checks)

### Phase 3: Array & Object Enhancements (2 weeks)

**Scope:**
- Tuple types with positional validation
- `@unique` constraint for arrays
- `contains`, `minContains`, `maxContains`
- `@closed` for objects (no additional properties)
- `minProperties`, `maxProperties`

### Phase 4: Conditional Validation (2-3 weeks)

**Scope:**
- `@if`/`@then`/`@else` parsing and evaluation
- `@requiredIf` dependency constraints
- `@oneOf`, `@anyOf`, `@allOf` composition
- `not` negation

### Phase 5: Metadata & Tooling (1-2 weeks)

**Scope:**
- Documentation annotations (`@title`, `@description`, `@example`)
- `@deprecated`, `@readOnly`, `@writeOnly`
- `@default` value specification
- JSON Schema export command

---

## Syntax Summary

### Constraint Syntax

```
type_expr ::= base_type
            | base_type 'where' constraint_expr
            | base_type annotation*

constraint_expr ::= '{' constraint_list '}'
                  | comparison_expr
                  
constraint_list ::= constraint (',' constraint)*

constraint ::= IDENT ':' value
             | IDENT '(' args ')'

comparison_expr ::= value OP '_' OP value    // range: 0 <= _ <= 100
                  | OP value                  // simple: >= 0
                  | '_' OP value              // simple: _ > 0

annotation ::= '@' IDENT
             | '@' IDENT '(' args ')'
```

### Annotation Reference

| Annotation | Applies To | Description |
|------------|------------|-------------|
| `@format(name)` | string | Validate against named format |
| `@pattern(regex)` | string | Validate against regex |
| `@unique` | array | All items must be unique |
| `@closed` | map | No additional properties allowed |
| `@required` | field | Field must be present |
| `@optional` | field | Field may be absent |
| `@requiredIf(cond)` | field | Conditionally required |
| `@default(value)` | field | Default value if absent |
| `@deprecated(msg)` | any | Mark as deprecated |
| `@readOnly` | field | Cannot be modified |
| `@writeOnly` | field | Not included in output |
| `@title(str)` | type/field | Human-readable title |
| `@description(str)` | type/field | Documentation |
| `@example(value)` | type/field | Example value |
| `@const` | type | Exact value required |
| `@enum(...)` | type | Allowed values |
| `@oneOf(...)` | type | Exactly one must match |
| `@anyOf(...)` | type | At least one must match |
| `@allOf` | type | All must match |
| `@if(cond)` | block | Conditional validation |
| `@ref(path)` | type | Schema reference |

---

## JSON Schema Interoperability

### Import from JSON Schema

```bash
# Convert JSON Schema to Lambda schema
lambda schema import user.schema.json -o user.ls
```

### Export to JSON Schema

```bash
# Convert Lambda schema to JSON Schema
lambda schema export user.ls -o user.schema.json --draft 2020-12
```

### Mapping Table

| JSON Schema | Lambda Equivalent |
|-------------|-------------------|
| `"type": "string"` | `string` |
| `"type": "integer"` | `int` |
| `"type": "number"` | `float` |
| `"type": "boolean"` | `bool` |
| `"type": "null"` | `null` |
| `"type": "array"` | `any[]` or `T[]` |
| `"type": "object"` | `{}` or `{...}` |
| `"type": ["string", "null"]` | `string?` or `string \| null` |
| `"minLength": 1` | `string where len >= 1` |
| `"pattern": "^a"` | `string @pattern("^a")` |
| `"minimum": 0` | `int where >= 0` |
| `"items": {...}` | `T[]` |
| `"uniqueItems": true` | `T[] @unique` |
| `"required": ["a"]` | `a: T` (required by default) |
| `"additionalProperties": false` | `{...} @closed` |
| `"format": "email"` | `string @format("email")` |
| `"enum": [...]` | `@enum(...)` |
| `"const": "x"` | `"x" @const` or literal type |
| `"$ref": "#/..."` | `TypeName` or `@ref(...)` |
| `"oneOf": [...]` | `@oneOf(...)` |
| `"anyOf": [...]` | `T \| U` or `@anyOf(...)` |
| `"allOf": [...]` | `T & U` or `@allOf` |
| `"not": {...}` | `where not(...)` |
| `"if"/"then"/"else"` | `@if(...)` blocks |

---

## Backward Compatibility

All proposed features are **additive**. Existing Lambda schemas will continue to work without modification:

- `where` clause is optional
- Annotations are optional
- Default behavior matches current semantics
- No breaking changes to existing type syntax

---

## Success Metrics

1. **Feature Coverage**: 100% of JSON Schema draft 2020-12 keywords mapped
2. **Round-trip Fidelity**: JSON Schema → Lambda → JSON Schema preserves semantics
3. **Performance**: Constraint validation adds < 10% overhead
4. **Test Coverage**: 95%+ for new validation features
5. **Documentation**: All features documented with examples

---

## Open Questions

1. **Constraint Expression Syntax**: Should we use `where` or `with` or `|>`?
2. **Annotation Prefix**: Should annotations use `@` or `#` or something else?
3. **Strictness Default**: Should objects be closed by default (like TypeScript strict mode)?
4. **Format Extensibility**: How to handle custom format registration at runtime vs compile time?
5. **Error Messages**: How detailed should constraint violation messages be?

---

## References

- [JSON Schema Draft 2020-12](https://json-schema.org/draft/2020-12/json-schema-core.html)
- [JSON Schema Validation](https://json-schema.org/draft/2020-12/json-schema-validation.html)
- [Current Lambda Validator API](./Lambda_Validator.md)
- [Lambda Type System](../doc/Lambda_Reference.md)
