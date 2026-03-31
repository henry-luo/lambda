// openapi/schema.ls — Convert OpenAPI JSON Schema definitions to Lambda type schemas
//
// Reads an OpenAPI spec (parsed from YAML/JSON) and produces Lambda
// type schema definitions that can be used with load_schema / validate.

import util: .util

// ============================================================
// Public API
// ============================================================

// Convert all component schemas from an OpenAPI spec into a
// Lambda schema definition string suitable for load_schema().
//
// spec: parsed OpenAPI spec (map)
// returns: string containing Lambda type definitions
pub fn spec_to_schema(spec) {
    let schemas = spec.components.schemas;
    if (schemas == null) ""
    else {
        let defs = for (type_name, schema_obj at schemas)
            convert_type(string(type_name), schema_obj, schemas);
        join(defs, "\n\n")
    }
}

// Convert a single named JSON Schema object into a Lambda type definition.
//
// type_name: name for the type (e.g. "Pet")
// schema_obj: the JSON Schema object
// all_schemas: the full components.schemas map for resolving $ref
// returns: string with "type TypeName = ..." definition
pub fn convert_type(type_name, schema_obj, all_schemas) {
    let body = convert_schema(schema_obj, all_schemas);
    "type " ++ type_name ++ " = " ++ body
}

// ============================================================
// Schema conversion (recursive)
// ============================================================

// Convert a JSON Schema object to a Lambda type expression string.
fn convert_schema(schema, all_schemas) {
    // handle $ref
    let ref = schema["$ref"];
    if (ref != null) util.ref_to_name(ref)

    // handle allOf (intersection/merge)
    else if (schema.allOf != null) convert_all_of(schema.allOf, all_schemas)

    // handle oneOf / anyOf (union)
    else if (schema.oneOf != null) convert_union(schema.oneOf, all_schemas)
    else if (schema.anyOf != null) convert_union(schema.anyOf, all_schemas)

    // handle enum
    else if (schema.enum != null) convert_enum(schema.enum)

    // handle object type
    else if (schema.type == "object" or schema.properties != null)
        convert_object(schema, all_schemas)

    // handle array type
    else if (schema.type == "array")
        convert_array(schema, all_schemas)

    // handle primitive types
    else if (schema.type != null)
        util.schema_type_name(schema.type, schema.format)

    // fallback: any
    else "any"
}

// ============================================================
// Object schema → Lambda map type
// ============================================================

fn convert_object(schema, all_schemas) {
    let props = schema.properties;
    if (props == null) "{}"
    else {
        let required = schema.required;
        let fields = for (field_name, field_schema at props) {
            let type_str = convert_schema(field_schema, all_schemas);
            let is_required = util.list_contains(required, string(field_name));
            let suffix = if (is_required) "" else "?";
            "    " ++ string(field_name) ++ ": " ++ type_str ++ suffix
        };
        "{\n" ++ join(fields, ",\n") ++ "\n}"
    }
}

// ============================================================
// Array schema → Lambda array type
// ============================================================

fn convert_array(schema, all_schemas) {
    let items = schema.items;
    if (items == null) "any[]"
    else {
        let item_type = convert_schema(items, all_schemas);
        // check minItems for occurrence
        let min_items = util.get_or(schema, "minItems", 0);
        if (min_items > 0) "[" ++ item_type ++ "+]"
        else item_type ++ "[]"
    }
}

// ============================================================
// Union types (oneOf / anyOf)
// ============================================================

fn convert_union(variants, all_schemas) {
    let types = for (v in variants) convert_schema(v, all_schemas);
    join(types, " | ")
}

// ============================================================
// allOf (merge all properties)
// ============================================================

fn convert_all_of(schemas, all_schemas) {
    // collect all property maps and required arrays, then merge
    let parts = for (s in schemas) convert_schema(s, all_schemas);

    // if all parts are type references, emit first (inheritance)
    // for now, join as union — the validator resolves structurally
    if (len(parts) == 1) parts[0]
    else join(parts, " | ")
}

// ============================================================
// Enum → Lambda union of literal values
// ============================================================

fn convert_enum(values) {
    let items = for (v in values) {
        if (v is string) "\"" ++ v ++ "\""
        else string(v)
    };
    join(items, " | ")
}

// ============================================================
// Extract request/response schemas for a specific path+method
// ============================================================

// Get the request body schema reference for a path operation.
//
// spec: parsed OpenAPI spec
// path: e.g. "/pets"
// method: e.g. "post"
// returns: schema map or null
pub fn request_schema(spec, path, method) {
    let op = spec.paths[path][method];
    if (op == null) null
    else {
        let content = op.requestBody.content;
        if (content == null) null
        else {
            let json_ct = content["application/json"];
            if (json_ct == null) null
            else resolve_ref(json_ct.schema, spec.components.schemas)
        }
    }
}

// Get the response schema for a path operation and status code.
//
// spec: parsed OpenAPI spec
// path: e.g. "/pets"
// method: e.g. "get"
// status: e.g. "200"
// returns: schema map or null
pub fn response_schema(spec, path, method, status) {
    let op = spec.paths[path][method];
    if (op == null) null
    else {
        let resp = op.responses[status];
        if (resp == null) null
        else {
            let content = resp.content;
            if (content == null) null
            else {
                let json_ct = content["application/json"];
                if (json_ct == null) null
                else resolve_ref(json_ct.schema, spec.components.schemas)
            }
        }
    }
}

// Resolve a $ref in a schema object, returning the referenced schema.
fn resolve_ref(schema, all_schemas) {
    if (schema == null) null
    else {
        let ref = schema["$ref"];
        if (ref != null) {
            let ref_name = util.ref_to_name(ref);
            all_schemas[ref_name]
        }
        else schema
    }
}

// ============================================================
// Extract parameter schemas for a path operation
// ============================================================

// Get parameter definitions for a path operation.
//
// spec: parsed OpenAPI spec
// path: e.g. "/pets/{petId}"
// method: e.g. "get"
// returns: array of {name, in, required, schema} maps
pub fn param_schemas(spec, path, method) {
    let op = spec.paths[path][method];
    if (op == null) []
    else {
        let params = util.get_or(op, "parameters", []);
        for (p in params) {
            name: string(p.name),
            location: string(p["in"]),
            required: util.get_or(p, "required", false),
            schema_type: if (p.schema != null)
                util.schema_type_name(p.schema.type, p.schema.format)
                else "string"
        }
    }
}
