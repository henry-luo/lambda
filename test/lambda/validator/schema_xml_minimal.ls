// Minimal XML schema for simple XML validation  
// Super restrictive - requires exact match

type Document = <root "Hello XML">   // Root element must contain exactly "Hello XML"

// Additional primitive types to demonstrate schema features  
type StringField = string            // primitive type
type NumberField = int              // primitive type
type BoolField = bool               // primitive type

// Optional field examples for schema features
type OptionalExample = {
    required: string,                // required field
    optional: string?                // optional field (for ? feature)
}
