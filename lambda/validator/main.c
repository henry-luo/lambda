/**
 * @file main.c
 * @brief Lambda Schema Validator CLI Tool
 * @author Henry Luo
 * @license MIT
 */

#include "validator.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

// CLI options
typedef struct {
    char* schema_file;
    char* document_file;
    char* schema_name;
    bool strict_mode;
    bool verbose;
    bool show_warnings;
    bool allow_unknown_fields;
} CLIOptions;

void print_usage(const char* program_name) {
    printf("Usage: %s [OPTIONS] -s SCHEMA_FILE -d DOCUMENT_FILE\n", program_name);
    printf("\nOptions:\n");
    printf("  -s, --schema FILE      Schema file to load\n");
    printf("  -d, --document FILE    Document file to validate\n");
    printf("  -n, --name NAME        Schema name to use (default: 'doc')\n");
    printf("  -S, --strict           Enable strict mode\n");
    printf("  -v, --verbose          Verbose output\n");
    printf("  -w, --warnings         Show warnings\n");
    printf("  -u, --unknown-fields   Allow unknown fields\n");
    printf("  -h, --help            Show this help message\n");
    printf("\nExamples:\n");
    printf("  %s -s doc_schema.ls -d sample.mark\n", program_name);
    printf("  %s -s schema.ls -d document.mark -n MyDoc --strict\n", program_name);
}

int parse_options(int argc, char* argv[], CLIOptions* options) {
    // Initialize defaults
    memset(options, 0, sizeof(CLIOptions));
    options->schema_name = "doc";
    options->allow_unknown_fields = true;
    
    static struct option long_options[] = {
        {"schema",          required_argument, 0, 's'},
        {"document",        required_argument, 0, 'd'},
        {"name",            required_argument, 0, 'n'},
        {"strict",          no_argument,       0, 'S'},
        {"verbose",         no_argument,       0, 'v'},
        {"warnings",        no_argument,       0, 'w'},
        {"unknown-fields",  no_argument,       0, 'u'},
        {"help",            no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };
    
    int c;
    while ((c = getopt_long(argc, argv, "s:d:n:Svwuh", long_options, NULL)) != -1) {
        switch (c) {
            case 's':
                options->schema_file = optarg;
                break;
            case 'd':
                options->document_file = optarg;
                break;
            case 'n':
                options->schema_name = optarg;
                break;
            case 'S':
                options->strict_mode = true;
                break;
            case 'v':
                options->verbose = true;
                break;
            case 'w':
                options->show_warnings = true;
                break;
            case 'u':
                options->allow_unknown_fields = true;
                break;
            case 'h':
                print_usage(argv[0]);
                return 1;
            case '?':
                return -1;
            default:
                return -1;
        }
    }
    
    // Validate required options
    if (!options->schema_file || !options->document_file) {
        fprintf(stderr, "Error: Both schema file (-s) and document file (-d) are required\n");
        return -1;
    }
    
    return 0;
}

char* read_file(const char* filename) {
    FILE* file = fopen(filename, "r");
    if (!file) {
        fprintf(stderr, "Error: Cannot open file %s\n", filename);
        return NULL;
    }
    
    // Get file size
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    // Allocate buffer and read
    char* content = malloc(size + 1);
    if (!content) {
        fprintf(stderr, "Error: Cannot allocate memory for file %s\n", filename);
        fclose(file);
        return NULL;
    }
    
    fread(content, 1, size, file);
    content[size] = '\0';
    fclose(file);
    
    return content;
}

void print_validation_result(LambdaValidationResult* result, bool verbose, bool show_warnings) {
    if (result->valid) {
        printf("✓ Document is valid!\n");
    } else {
        printf("✗ Document validation failed\n");
    }
    
    // Print errors
    if (result->error_count > 0) {
        printf("\nErrors (%d):\n", result->error_count);
        for (int i = 0; i < result->error_count; i++) {
            printf("  %d. %s\n", i + 1, result->errors[i]);
        }
    }
    
    // Print warnings if requested
    if (show_warnings && result->warning_count > 0) {
        printf("\nWarnings (%d):\n", result->warning_count);
        for (int i = 0; i < result->warning_count; i++) {
            printf("  %d. %s\n", i + 1, result->warnings[i]);
        }
    }
    
    if (verbose) {
        printf("\nValidation Summary:\n");
        printf("  Total Errors: %d\n", result->error_count);
        printf("  Total Warnings: %d\n", result->warning_count);
    }
}

int main(int argc, char* argv[]) {
    CLIOptions options;
    int parse_result = parse_options(argc, argv, &options);
    
    if (parse_result == 1) {
        return 0; // Help was shown
    } else if (parse_result == -1) {
        return 1; // Error parsing options
    }
    
    if (options.verbose) {
        printf("Lambda Schema Validator\n");
        printf("Schema file: %s\n", options.schema_file);
        printf("Document file: %s\n", options.document_file);
        printf("Schema name: %s\n", options.schema_name);
        printf("Strict mode: %s\n", options.strict_mode ? "enabled" : "disabled");
        printf("\n");
    }
    
    // Create validator
    LambdaValidator* validator = lambda_validator_create();
    if (!validator) {
        fprintf(stderr, "Error: Failed to create validator\n");
        return 1;
    }
    
    // Set validation options
    LambdaValidationOptions val_options = {
        .strict_mode = options.strict_mode,
        .allow_unknown_fields = options.allow_unknown_fields,
        .allow_empty_elements = false,
        .max_validation_depth = 100,
        .enabled_custom_rules = NULL,
        .disabled_rules = NULL
    };
    lambda_validator_set_options(validator, &val_options);
    
    // Load schema
    if (options.verbose) {
        printf("Loading schema from %s...\n", options.schema_file);
    }
    
    if (lambda_validator_load_schema_file(validator, options.schema_file) != 0) {
        fprintf(stderr, "Error: Failed to load schema from %s\n", options.schema_file);
        lambda_validator_destroy(validator);
        return 1;
    }
    
    if (options.verbose) {
        printf("Schema loaded successfully.\n");
    }
    
    // Validate document
    if (options.verbose) {
        printf("Validating document %s...\n", options.document_file);
    }
    
    LambdaValidationResult* result = lambda_validate_file(
        validator, options.document_file, options.schema_name);
    
    if (!result) {
        fprintf(stderr, "Error: Failed to validate document\n");
        lambda_validator_destroy(validator);
        return 1;
    }
    
    // Print results
    print_validation_result(result, options.verbose, options.show_warnings);
    
    // Cleanup
    int exit_code = result->valid ? 0 : 1;
    lambda_validation_result_free(result);
    lambda_validator_destroy(validator);
    
    return exit_code;
}

// Example usage function for demonstration
void example_usage() {
    printf("Example: Validating a document programmatically\n\n");
    
    // Create validator
    LambdaValidator* validator = lambda_validator_create();
    
    // Load schema from string
    const char* schema_source = 
        "// Simple schema example\n"
        "type SimpleDoc < \n"
        "    title: string,\n"
        "    content: [string*]\n"
        ">";
    
    lambda_validator_load_schema_string(validator, schema_source, "SimpleDoc");
    
    // Validate document from string
    const char* document_source = 
        "<SimpleDoc title:\"Test Document\"\n"
        "    \"This is content\"\n"
        "    \"More content\"\n"
        ">";
    
    LambdaValidationResult* result = lambda_validate_string(
        validator, document_source, "SimpleDoc");
    
    if (result->valid) {
        printf("✓ Example document is valid!\n");
    } else {
        printf("✗ Example document has errors:\n");
        for (int i = 0; i < result->error_count; i++) {
            printf("  - %s\n", result->errors[i]);
        }
    }
    
    // Cleanup
    lambda_validation_result_free(result);
    lambda_validator_destroy(validator);
}

// Test function for development
#ifdef DEBUG
void run_tests() {
    printf("Running validator tests...\n");
    
    // TODO: Add comprehensive tests
    printf("Tests completed.\n");
}
#endif
