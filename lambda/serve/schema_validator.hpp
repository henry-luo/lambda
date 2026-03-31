/**
 * @file schema_validator.hpp
 * @brief JSON Schema draft 2020-12 subset validator for request/response bodies
 */

#ifndef LAMBDA_SERVE_SCHEMA_VALIDATOR_HPP
#define LAMBDA_SERVE_SCHEMA_VALIDATOR_HPP

#include "rest.hpp"
#include "middleware.hpp"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Validation Error
// ============================================================================

#define MAX_VALIDATION_ERRORS 32

typedef struct ValidationError {
    const char *path;       // JSON pointer (e.g., "/email")
    const char *message;    // error description
} ValidationError;

typedef struct ValidationResult {
    int             valid;
    ValidationError errors[MAX_VALIDATION_ERRORS];
    int             error_count;
} ValidationResult;

// ============================================================================
// API
// ============================================================================

/**
 * Validate a JSON string against a JSON Schema string.
 * Supports a subset of JSON Schema draft 2020-12:
 *   - type checking (string, number, integer, boolean, array, object, null)
 *   - required fields
 *   - string constraints: minLength, maxLength, pattern
 *   - numeric constraints: minimum, maximum, multipleOf
 *   - array constraints: minItems, maxItems
 *   - enum values
 *   - properties / additionalProperties
 *   - items (array item schema)
 *
 * Result is written to `result`. Returns 1 if valid, 0 if invalid.
 */
int schema_validate(const char *json_data, const char *schema_json,
                    ValidationResult *result);

/**
 * Write validation errors as a JSON response body (HTTP 422 format).
 * Returns a malloc'd string the caller must free.
 */
char* schema_validation_error_json(const ValidationResult *result);

/**
 * Create a middleware that validates request bodies against schemas
 * registered in the endpoint registry.
 * The middleware checks if the matched route has a request_schema and
 * validates the body before passing to the handler.
 */
MiddlewareFn schema_validation_middleware(void);

#ifdef __cplusplus
}
#endif

#endif // LAMBDA_SERVE_SCHEMA_VALIDATOR_HPP
