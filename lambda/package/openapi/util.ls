// openapi/util.ls — Shared utility functions for OpenAPI package

// ============================================================
// String helpers
// ============================================================

// join path segments with "/"
pub fn join_path(parts) => join(parts, "/")

// capitalize first character of a string
pub fn capitalize(s) {
    if (len(s) == 0) ""
    else s[0 to 0] ++ s[1 to len(s) - 1]
}

// convert a reference like "#/components/schemas/Pet" to just "Pet"
pub fn ref_to_name(ref_str) {
    let parts = split(ref_str, "/");
    parts[len(parts) - 1]
}

// ============================================================
// Map helpers
// ============================================================

// safely get a nested field, returning fallback if any part is null
pub fn get_or(m, key, fallback) {
    let v = m[key];
    if (v == null) fallback else v
}

// check if a value is in an array
pub fn list_contains(arr, val) {
    if (arr == null) false
    else any(arr | ~ == val)
}

// ============================================================
// HTTP method list (standard)
// ============================================================

pub METHODS = ["get", "post", "put", "delete", "patch", "head", "options"]

// ============================================================
// JSON Schema type to Lambda type name mapping
// ============================================================

pub fn schema_type_name(json_type, format) => match json_type {
    case "string" {
        if (format == "date-time") "datetime"
        else if (format == "date") "string"
        else "string"
    }
    case "integer": "int"
    case "number": "float"
    case "boolean": "bool"
    default: json_type
}
