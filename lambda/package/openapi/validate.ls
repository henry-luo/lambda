// openapi/validate.ls — Request/response validation against OpenAPI schemas
//
// Provides functions to validate HTTP request bodies, query params,
// and response bodies against schemas extracted from an OpenAPI spec.

import schema: .schema
import util: .util

// ============================================================
// Public API
// ============================================================

// Validate a request body against the OpenAPI spec for a given path and method.
//
// spec: parsed OpenAPI spec
// path: route path, e.g. "/pets"
// method: HTTP method, e.g. "post"
// body: the parsed request body (map/array)
// returns: {valid: bool, errors: [...]}
pub fn validate_request(spec, path, method, body) {
    let req_schema = schema.request_schema(spec, path, method);
    if (req_schema == null)
        {valid: true, errors: []}
    else
        validate_against(req_schema, body, spec.components.schemas)
}

// Validate a response body against the OpenAPI spec.
//
// spec: parsed OpenAPI spec
// path: route path
// method: HTTP method
// status: status code string, e.g. "200"
// body: the parsed response body
// returns: {valid: bool, errors: [...]}
pub fn validate_response(spec, path, method, status, body) {
    let resp_schema = schema.response_schema(spec, path, method, status);
    if (resp_schema == null)
        {valid: true, errors: []}
    else
        validate_against(resp_schema, body, spec.components.schemas)
}

// Validate query/path parameters for a request.
//
// spec: parsed OpenAPI spec
// path: route path
// method: HTTP method
// params: map of parameter name → value (strings from query/path)
// returns: {valid: bool, errors: [...]}
pub fn validate_params(spec, path, method, params) {
    let param_defs = schema.param_schemas(spec, path, method);
    if (len(param_defs) == 0)
        {valid: true, errors: []}
    else {
        let errors = for (p in param_defs where p.required) {
            let val = params[p.name];
            if (val == null)
                {
                    path: p.name,
                    message: "required " ++ p.location ++ " parameter missing",
                    expected: p.schema_type,
                    actual: "null"
                }
            else null
        };
        // filter out nulls
        let real_errors = errors that (~ != null);
        {valid: len(real_errors) == 0, errors: real_errors}
    }
}

// ============================================================
// Schema-based validation (structural)
// ============================================================

// Validate a value against a JSON Schema object from the OpenAPI spec.
// This performs structural type checking without using load_schema —
// it walks the JSON Schema directly.
//
// schema_obj: JSON Schema map (from components.schemas)
// value: the data to validate
// all_schemas: components.schemas for resolving $ref
// returns: {valid: bool, errors: [...]}
fn validate_against(schema_obj, value, all_schemas) {
    let errors = check_value(schema_obj, value, all_schemas, "");
    {valid: len(errors) == 0, errors: errors}
}

// Recursive value checker
fn check_value(schema_obj, value, all_schemas, path) {
    // resolve $ref
    let ref = schema_obj["$ref"];
    if (ref != null) {
        let ref_name = util.ref_to_name(ref);
        let resolved = all_schemas[ref_name];
        if (resolved == null)
            [{path: path, message: "unresolved reference: " ++ ref_name,
              expected: ref_name, actual: string(type(value))}]
        else
            check_value(resolved, value, all_schemas, path)
    }

    // handle allOf
    else if (schema_obj.allOf != null) {
        for (sub in schema_obj.allOf)
            check_value(sub, value, all_schemas, path)
    }

    // handle oneOf
    else if (schema_obj.oneOf != null)
        check_one_of(schema_obj.oneOf, value, all_schemas, path)

    // handle anyOf
    else if (schema_obj.anyOf != null)
        check_any_of(schema_obj.anyOf, value, all_schemas, path)

    // handle enum
    else if (schema_obj.enum != null)
        check_enum(schema_obj.enum, value, path)

    // handle object
    else if (schema_obj.type == "object" or schema_obj.properties != null)
        check_object(schema_obj, value, all_schemas, path)

    // handle array
    else if (schema_obj.type == "array")
        check_array(schema_obj, value, all_schemas, path)

    // handle primitives
    else if (schema_obj.type != null)
        check_primitive(schema_obj, value, path)

    // no schema constraint
    else []
}

// ============================================================
// Object validation
// ============================================================

fn check_object(schema_obj, value, all_schemas, path) {
    if (type(value) != 'map')
        [{path: path, message: "type mismatch",
          expected: "object", actual: string(type(value))}]
    else {
        let props = schema_obj.properties;
        let required = util.get_or(schema_obj, "required", []);

        // check required fields exist
        let req_errors = if (props != null) {
            for (field_name, _ at props
                 where util.list_contains(required, string(field_name))) {
                let field_path = if (len(path) > 0) path ++ "." ++ string(field_name)
                                 else string(field_name);
                if (value[string(field_name)] == null)
                    {path: field_path, message: "required field missing",
                     expected: "present", actual: "null"}
                else null
            }
        } else [];

        // check types of present fields
        let type_errors = if (props != null) {
            for (field_name, field_schema at props
                 where value[string(field_name)] != null) {
                let field_path = if (len(path) > 0) path ++ "." ++ string(field_name)
                                 else string(field_name);
                check_value(field_schema, value[string(field_name)], all_schemas, field_path)
            }
        } else [];

        // combine errors, filtering nulls
        let all_errors = [req_errors that (~ != null), type_errors that (~ != null)];
        all_errors that (~ != null)
    }
}

// ============================================================
// Array validation
// ============================================================

fn check_array(schema_obj, value, all_schemas, path) {
    if (type(value) != 'array')
        [{path: path, message: "type mismatch",
          expected: "array", actual: string(type(value))}]
    else {
        let items_schema = schema_obj.items;
        let min_items = util.get_or(schema_obj, "minItems", 0);
        let max_items = util.get_or(schema_obj, "maxItems", -1);

        let len_errors = [
            if (len(value) < min_items)
                {path: path, message: "array too short",
                 expected: "minItems " ++ string(min_items),
                 actual: string(len(value))}
            else null,
            if (max_items >= 0 and len(value) > max_items)
                {path: path, message: "array too long",
                 expected: "maxItems " ++ string(max_items),
                 actual: string(len(value))}
            else null
        ];

        let item_errors = if (items_schema != null) {
            for (i in 0 to len(value) - 1 where len(value) > 0) {
                let item_path = path ++ "[" ++ string(i) ++ "]";
                check_value(items_schema, value[i], all_schemas, item_path)
            }
        } else [];

        [len_errors that (~ != null), item_errors that (~ != null)]
            that (~ != null)
    }
}

// ============================================================
// Enum validation
// ============================================================

fn check_enum(allowed, value, path) {
    if (util.list_contains(allowed, value)) []
    else [{path: path, message: "value not in enum",
           expected: join(allowed | string(~), " | "),
           actual: string(value)}]
}

// ============================================================
// oneOf / anyOf validation
// ============================================================

fn check_one_of(variants, value, all_schemas, path) {
    let matches = for (v in variants
                       where len(check_value(v, value, all_schemas, path)) == 0) v;
    if (len(matches) == 1) []
    else if (len(matches) == 0)
        [{path: path, message: "value matches none of oneOf variants",
          expected: "one of " ++ string(len(variants)) ++ " variants",
          actual: string(type(value))}]
    else
        [{path: path, message: "value matches multiple oneOf variants",
          expected: "exactly one variant",
          actual: string(len(matches)) ++ " variants matched"}]
}

fn check_any_of(variants, value, all_schemas, path) {
    let matches = for (v in variants
                       where len(check_value(v, value, all_schemas, path)) == 0) v;
    if (len(matches) > 0) []
    else [{path: path, message: "value matches none of anyOf variants",
           expected: "at least one of " ++ string(len(variants)) ++ " variants",
           actual: string(type(value))}]
}

// ============================================================
// Primitive type validation
// ============================================================

fn check_primitive(schema_obj, value, path) {
    let expected = schema_obj.type;
    let actual_type = type(value);
    let ok = match expected {
        case "string": actual_type == 'string'
        case "integer": actual_type == 'int'
        case "number": actual_type == 'int' or actual_type == 'float'
        case "boolean": actual_type == 'bool'
        default: true
    };
    if (ok) {
        // check string constraints
        let str_errors = if (expected == "string" and actual_type == 'string') {
            let min_len = util.get_or(schema_obj, "minLength", -1);
            let max_len = util.get_or(schema_obj, "maxLength", -1);
            [
                if (min_len >= 0 and len(value) < min_len)
                    {path: path, message: "string too short",
                     expected: "minLength " ++ string(min_len),
                     actual: string(len(value))}
                else null,
                if (max_len >= 0 and len(value) > max_len)
                    {path: path, message: "string too long",
                     expected: "maxLength " ++ string(max_len),
                     actual: string(len(value))}
                else null
            ] that (~ != null)
        } else [];
        // check numeric constraints
        let num_errors = if ((expected == "integer" or expected == "number")
                             and (actual_type == 'int' or actual_type == 'float')) {
            let minimum = util.get_or(schema_obj, "minimum", null);
            let maximum = util.get_or(schema_obj, "maximum", null);
            [
                if (minimum != null and value < minimum)
                    {path: path, message: "value below minimum",
                     expected: "minimum " ++ string(minimum),
                     actual: string(value)}
                else null,
                if (maximum != null and value > maximum)
                    {path: path, message: "value above maximum",
                     expected: "maximum " ++ string(maximum),
                     actual: string(value)}
                else null
            ] that (~ != null)
        } else [];
        [str_errors, num_errors] that (~ != null)
    }
    else
        [{path: path, message: "type mismatch",
          expected: expected, actual: string(actual_type)}]
}
