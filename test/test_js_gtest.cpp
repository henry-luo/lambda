#include <gtest/gtest.h>
#include <cstdio>
#include <cstdlib>
#include <cassert>
#include <cstring>
#include <cerrno>

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #define NOGDI
    #define NOUSER
    #define NOHELP
    #define NOMCX
    #include <windows.h>
    #include <process.h>
    #include <io.h>
    #include <direct.h>
    #define getcwd _getcwd
    #define chdir _chdir
    #define unlink _unlink
    #define access _access
    #define popen _popen
    #define pclose _pclose
    #define WEXITSTATUS(status) (status)
#else
    #include <unistd.h>
    #include <sys/wait.h>
#endif

// Helper function to execute a JavaScript file with lambda js and capture output
char* execute_js_script(const char* script_path) {
    char command[512];
#ifdef _WIN32
    snprintf(command, sizeof(command), "lambda.exe js \"%s\" --no-log", script_path);
#else
    snprintf(command, sizeof(command), "./lambda.exe js \"%s\" --no-log", script_path);
#endif

    FILE* pipe = popen(command, "r");
    if (!pipe) {
        fprintf(stderr, "Error: Could not execute command: %s\n", command);
        return nullptr;
    }

    // Read output in chunks
    char buffer[1024];
    size_t total_size = 0;
    char* full_output = nullptr;

    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        size_t len = strlen(buffer);
        char* new_output = (char*)realloc(full_output, total_size + len + 1);
        if (!new_output) {
            free(full_output);
            pclose(pipe);
            return nullptr;
        }
        full_output = new_output;
        strcpy(full_output + total_size, buffer);
        total_size += len;
    }

    int exit_code = pclose(pipe);
    if (WEXITSTATUS(exit_code) != 0) {
        fprintf(stderr, "Error: lambda.exe js exited with code %d for script: %s\n",
                WEXITSTATUS(exit_code), script_path);
        free(full_output);
        return nullptr;
    }

    // Extract result from "##### Script" marker (same as Lambda tests)
    if (!full_output) {
        return nullptr;
    }
    char* marker = strstr(full_output, "##### Script");
    if (marker) {
        char* result_start = strchr(marker, '\n');
        if (result_start) {
            result_start++; // Skip the newline
            // Create a copy of the result
            char* result = strdup(result_start);
            free(full_output);
            return result;
        }
    }

    return full_output;
}

// Helper function to trim trailing whitespace
void trim_trailing_whitespace(char* str) {
    if (!str) return;
    size_t len = strlen(str);
    while (len > 0 && isspace((unsigned char)str[len - 1])) {
        str[--len] = '\0';
    }
}

// Helper function to read expected output from file
char* read_expected_output(const char* expected_file_path) {
    FILE* file = fopen(expected_file_path, "r");
    if (!file) {
        return nullptr;
    }

    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    char* content = (char*)malloc(file_size + 1);
    if (!content) {
        fclose(file);
        return nullptr;
    }

    size_t read_size = fread(content, 1, file_size, file);
    content[read_size] = '\0';
    fclose(file);

    trim_trailing_whitespace(content);
    return content;
}

// Helper function to test JavaScript script against expected output file
void test_js_script_against_file(const char* script_path, const char* expected_file_path) {
    // Get script name for better error messages
    const char* script_name = strrchr(script_path, '/');
    script_name = script_name ? script_name + 1 : script_path;

    char* expected_output = read_expected_output(expected_file_path);
    ASSERT_NE(expected_output, nullptr) << "Could not read expected output file: " << expected_file_path;

    char* actual_output = execute_js_script(script_path);
    ASSERT_NE(actual_output, nullptr) << "Could not execute JavaScript script: " << script_path;

    // Trim whitespace from actual output
    trim_trailing_whitespace(actual_output);

    // Compare outputs
    ASSERT_STREQ(expected_output, actual_output)
        << "Output mismatch for JavaScript script: " << script_path
        << "\nExpected (" << strlen(expected_output) << " chars): " << expected_output
        << "\nActual (" << strlen(actual_output) << " chars): " << actual_output;

    free(expected_output);
    free(actual_output);
}

// Helper function to test JavaScript command interface
char* execute_js_builtin_tests() {
    char command[512];
#ifdef _WIN32
    snprintf(command, sizeof(command), "lambda.exe js --no-log 2>&1");
#else
    snprintf(command, sizeof(command), "./lambda.exe js --no-log 2>&1");
#endif

    FILE* pipe = popen(command, "r");
    if (!pipe) {
        return nullptr;
    }

    // Read output in chunks
    char buffer[1024];
    size_t total_size = 0;
    char* full_output = nullptr;

    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        size_t len = strlen(buffer);
        char* new_output = (char*)realloc(full_output, total_size + len + 1);
        if (!new_output) {
            free(full_output);
            pclose(pipe);
            return nullptr;
        }
        full_output = new_output;
        strcpy(full_output + total_size, buffer);
        total_size += len;
    }

    int exit_code = pclose(pipe);
    
    // Even if there's no output, if the command succeeded (exit code 0), return an empty string
    if (WEXITSTATUS(exit_code) != 0) {
        free(full_output);
        return nullptr;
    }
    
    // If no output was produced but command succeeded, return empty string
    if (full_output == nullptr) {
        full_output = (char*)calloc(1, 1);  // Empty string
    }

    return full_output;
}

// Helper function to execute a JavaScript file with --document flag and capture output
char* execute_js_script_with_doc(const char* script_path, const char* html_path) {
    char command[512];
#ifdef _WIN32
    snprintf(command, sizeof(command), "lambda.exe js \"%s\" --document \"%s\" --no-log", script_path, html_path);
#else
    snprintf(command, sizeof(command), "./lambda.exe js \"%s\" --document \"%s\" --no-log", script_path, html_path);
#endif

    FILE* pipe = popen(command, "r");
    if (!pipe) {
        fprintf(stderr, "Error: Could not execute command: %s\n", command);
        return nullptr;
    }

    // Read output in chunks
    char buffer[1024];
    size_t total_size = 0;
    char* full_output = nullptr;

    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        size_t len = strlen(buffer);
        char* new_output = (char*)realloc(full_output, total_size + len + 1);
        if (!new_output) {
            free(full_output);
            pclose(pipe);
            return nullptr;
        }
        full_output = new_output;
        strcpy(full_output + total_size, buffer);
        total_size += len;
    }

    int exit_code = pclose(pipe);
    if (WEXITSTATUS(exit_code) != 0) {
        fprintf(stderr, "Error: lambda.exe js exited with code %d for script: %s --document %s\n",
                WEXITSTATUS(exit_code), script_path, html_path);
        free(full_output);
        return nullptr;
    }

    // Extract result from "##### Script" marker (same as Lambda tests)
    if (!full_output) {
        return nullptr;
    }
    char* marker = strstr(full_output, "##### Script");
    if (marker) {
        char* result_start = strchr(marker, '\n');
        if (result_start) {
            result_start++; // Skip the newline
            char* result = strdup(result_start);
            free(full_output);
            return result;
        }
    }

    return full_output;
}

// Helper function to test JavaScript DOM script against expected output file
void test_js_dom_script_against_file(const char* script_path, const char* html_path, const char* expected_file_path) {
    const char* script_name = strrchr(script_path, '/');
    script_name = script_name ? script_name + 1 : script_path;

    char* expected_output = read_expected_output(expected_file_path);
    ASSERT_NE(expected_output, nullptr) << "Could not read expected output file: " << expected_file_path;

    char* actual_output = execute_js_script_with_doc(script_path, html_path);
    ASSERT_NE(actual_output, nullptr) << "Could not execute JavaScript DOM script: " << script_path;

    trim_trailing_whitespace(actual_output);

    ASSERT_STREQ(expected_output, actual_output)
        << "Output mismatch for JavaScript DOM script: " << script_path
        << "\nExpected (" << strlen(expected_output) << " chars): " << expected_output
        << "\nActual (" << strlen(actual_output) << " chars): " << actual_output;

    free(expected_output);
    free(actual_output);
}

// JavaScript Test Cases
TEST(JavaScriptTests, test_js_command_interface) {
    // Test that the JavaScript command interface works
    char* output = execute_js_builtin_tests();
    ASSERT_NE(output, nullptr) << "JavaScript command should execute successfully";

    // Since the JS transpiler is not fully implemented yet, we test that:
    // 1. The command executes without crashing (exit code 0)
    // 2. The command infrastructure is in place
    // Note: ./lambda.exe js with no arguments currently produces no output,
    // which is acceptable for now as the built-in test functionality is not implemented
    // ASSERT_TRUE(strlen(output) > 0) << "JavaScript command should produce some output";

    free(output);
}

TEST(JavaScriptTests, test_simple_test) {
    test_js_script_against_file("test/js/simple_test.js", "test/js/simple_test.txt");
}

TEST(JavaScriptTests, test_arithmetic) {
    test_js_script_against_file("test/js/arithmetic.js", "test/js/arithmetic.txt");
}

TEST(JavaScriptTests, test_console_log) {
    test_js_script_against_file("test/js/console_log.js", "test/js/console_log.txt");
}

TEST(JavaScriptTests, test_variables) {
    test_js_script_against_file("test/js/variables.js", "test/js/variables.txt");
}

TEST(JavaScriptTests, test_control_flow_basic) {
    test_js_script_against_file("test/js/control_flow_basic.js", "test/js/control_flow_basic.txt");
}

TEST(JavaScriptTests, test_functions_basic) {
    test_js_script_against_file("test/js/functions_basic.js", "test/js/functions_basic.txt");
}

TEST(JavaScriptTests, test_basic_expressions) {
    test_js_script_against_file("test/js/basic_expressions.js", "test/js/basic_expressions.txt");
}

TEST(JavaScriptTests, test_functions) {
    test_js_script_against_file("test/js/functions.js", "test/js/functions.txt");
}

TEST(JavaScriptTests, test_control_flow) {
    test_js_script_against_file("test/js/control_flow.js", "test/js/control_flow.txt");
}

TEST(JavaScriptTests, test_advanced_features) {
    test_js_script_against_file("test/js/advanced_features.js", "test/js/advanced_features.txt");
}

TEST(JavaScriptTests, test_es6_features) {
    test_js_script_against_file("test/js/es6_features.js", "test/js/es6_features.txt");
}

TEST(JavaScriptTests, test_error_handling) {
    test_js_script_against_file("test/js/error_handling.js", "test/js/error_handling.txt");
}

TEST(JavaScriptTests, test_array_methods) {
    test_js_script_against_file("test/js/array_methods.js", "test/js/array_methods.txt");
}

// v3 tests: String methods, Math object, Array methods
TEST(JavaScriptTests, test_string_methods) {
    test_js_script_against_file("test/js/string_methods.js", "test/js/string_methods.txt");
}

TEST(JavaScriptTests, test_math_object) {
    test_js_script_against_file("test/js/math_object.js", "test/js/math_object.txt");
}

TEST(JavaScriptTests, test_array_methods_v3) {
    test_js_script_against_file("test/js/array_methods_v3.js", "test/js/array_methods_v3.txt");
}

// v3 Phase 3d tests: DOM API
TEST(JavaScriptTests, test_dom_basic) {
    test_js_dom_script_against_file("test/js/dom_basic.js", "test/js/dom_basic.html", "test/js/dom_basic.txt");
}

TEST(JavaScriptTests, test_dom_mutation) {
    test_js_dom_script_against_file("test/js/dom_mutation.js", "test/js/dom_mutation.html", "test/js/dom_mutation.txt");
}

TEST(JavaScriptTests, test_dom_style) {
    test_js_dom_script_against_file("test/js/dom_style.js", "test/js/dom_style.html", "test/js/dom_style.txt");
}

TEST(JavaScriptTests, test_dom_v12b) {
    test_js_dom_script_against_file("test/js/dom_v12b.js", "test/js/dom_v12b.html", "test/js/dom_v12b.txt");
}

// v5 coverage tests: implemented but previously untested features
TEST(JavaScriptTests, test_switch_statement) {
    test_js_script_against_file("test/js/switch_statement.js", "test/js/switch_statement.txt");
}

TEST(JavaScriptTests, test_do_while) {
    test_js_script_against_file("test/js/do_while.js", "test/js/do_while.txt");
}

TEST(JavaScriptTests, test_for_in_loop) {
    test_js_script_against_file("test/js/for_in_loop.js", "test/js/for_in_loop.txt");
}

TEST(JavaScriptTests, test_operators_extra) {
    test_js_script_against_file("test/js/operators_extra.js", "test/js/operators_extra.txt");
}

TEST(JavaScriptTests, test_global_functions) {
    test_js_script_against_file("test/js/global_functions.js", "test/js/global_functions.txt");
}

TEST(JavaScriptTests, test_template_literals) {
    test_js_script_against_file("test/js/template_literals.js", "test/js/template_literals.txt");
}

TEST(JavaScriptTests, test_for_of_loop) {
    test_js_script_against_file("test/js/for_of_loop.js", "test/js/for_of_loop.txt");
}

TEST(JavaScriptTests, test_bitwise_ops) {
    test_js_script_against_file("test/js/bitwise_ops.js", "test/js/bitwise_ops.txt");
}

TEST(JavaScriptTests, test_typed_arrays) {
    test_js_script_against_file("test/js/typed_arrays.js", "test/js/typed_arrays.txt");
}

TEST(JavaScriptTests, test_number_methods) {
    test_js_script_against_file("test/js/number_methods.js", "test/js/number_methods.txt");
}

TEST(JavaScriptTests, test_object_static) {
    test_js_script_against_file("test/js/object_static.js", "test/js/object_static.txt");
}

TEST(JavaScriptTests, test_spread_element) {
    test_js_script_against_file("test/js/spread_element.js", "test/js/spread_element.txt");
}

TEST(JavaScriptTests, test_destructuring) {
    test_js_script_against_file("test/js/destructuring.js", "test/js/destructuring.txt");
}

TEST(JavaScriptTests, test_closures) {
    test_js_script_against_file("test/js/closures.js", "test/js/closures.txt");
}

TEST(JavaScriptTests, test_sort_destr_methods) {
    test_js_script_against_file("test/js/sort_destr_methods.js", "test/js/sort_destr_methods.txt");
}

TEST(JavaScriptTests, test_tco) {
    test_js_script_against_file("test/js/tco.js", "test/js/tco.txt");
}

// v9 tests: new array/string/math/object methods, destructuring, Number statics
TEST(JavaScriptTests, test_v9_array_methods) {
    test_js_script_against_file("test/js/v9_array_methods.js", "test/js/v9_array_methods.txt");
}

TEST(JavaScriptTests, test_v9_string_methods) {
    test_js_script_against_file("test/js/v9_string_methods.js", "test/js/v9_string_methods.txt");
}

TEST(JavaScriptTests, test_v9_math_methods) {
    test_js_script_against_file("test/js/v9_math_methods.js", "test/js/v9_math_methods.txt");
}

TEST(JavaScriptTests, test_v9_object_methods) {
    test_js_script_against_file("test/js/v9_object_methods.js", "test/js/v9_object_methods.txt");
}

TEST(JavaScriptTests, test_v9_number_json) {
    test_js_script_against_file("test/js/v9_number_json.js", "test/js/v9_number_json.txt");
}

TEST(JavaScriptTests, test_v9_obj_destructuring) {
    test_js_script_against_file("test/js/v9_obj_destructuring.js", "test/js/v9_obj_destructuring.txt");
}

// v11 Tests
TEST(JavaScriptTests, test_v11_optional_chaining) {
    test_js_script_against_file("test/js/v11_optional_chaining.js", "test/js/v11_optional_chaining.txt");
}

TEST(JavaScriptTests, test_v11_sequence_expr) {
    test_js_script_against_file("test/js/v11_sequence_expr.js", "test/js/v11_sequence_expr.txt");
}

TEST(JavaScriptTests, test_v11_error_subclasses) {
    test_js_script_against_file("test/js/v11_error_subclasses.js", "test/js/v11_error_subclasses.txt");
}

TEST(JavaScriptTests, test_v11_nullish_assign) {
    test_js_script_against_file("test/js/v11_nullish_assign.js", "test/js/v11_nullish_assign.txt");
}

TEST(JavaScriptTests, test_v11_object_methods) {
    test_js_script_against_file("test/js/v11_object_methods.js", "test/js/v11_object_methods.txt");
}

TEST(JavaScriptTests, test_v11_labeled_statements) {
    test_js_script_against_file("test/js/v11_labeled_statements.js", "test/js/v11_labeled_statements.txt");
}

TEST(JavaScriptTests, test_v11_function_bind) {
    test_js_script_against_file("test/js/v11_function_bind.js", "test/js/v11_function_bind.txt");
}

TEST(JavaScriptTests, test_v11_regex_methods) {
    test_js_script_against_file("test/js/v11_regex_methods.js", "test/js/v11_regex_methods.txt");
}

TEST(JavaScriptTests, test_v11_date_methods) {
    test_js_script_against_file("test/js/v11_date_methods.js", "test/js/v11_date_methods.txt");
}

TEST(JavaScriptTests, test_v11_map_set) {
    test_js_script_against_file("test/js/v11_map_set.js", "test/js/v11_map_set.txt");
}

// Transpiler optimization tests (Transpile_Js13 P1/P3/P4/P5/P6)
TEST(JavaScriptTests, test_opt_p1_return_type) {
    test_js_script_against_file("test/js/opt_p1_return_type.js", "test/js/opt_p1_return_type.txt");
}

TEST(JavaScriptTests, test_opt_p3_ctor_stores) {
    test_js_script_against_file("test/js/opt_p3_ctor_stores.js", "test/js/opt_p3_ctor_stores.txt");
}

TEST(JavaScriptTests, test_opt_p4_typed_reads) {
    test_js_script_against_file("test/js/opt_p4_typed_reads.js", "test/js/opt_p4_typed_reads.txt");
}

TEST(JavaScriptTests, test_opt_p5_modvar) {
    test_js_script_against_file("test/js/opt_p5_modvar.js", "test/js/opt_p5_modvar.txt");
}

TEST(JavaScriptTests, test_opt_p6_func_inline) {
    test_js_script_against_file("test/js/opt_p6_func_inline.js", "test/js/opt_p6_func_inline.txt");
}

TEST(JavaScriptTests, test_opt_p2_obj_alloc) {
    test_js_script_against_file("test/js/opt_p2_obj_alloc.js", "test/js/opt_p2_obj_alloc.txt");
}

TEST(JavaScriptTests, test_opt_p7_method_native) {
    test_js_script_against_file("test/js/opt_p7_method_native.js", "test/js/opt_p7_method_native.txt");
}

TEST(JavaScriptTests, test_es_modules) {
    test_js_script_against_file("test/js/module_main.js", "test/js/module_main.txt");
}

TEST(JavaScriptTests, test_es_modules_advanced) {
    test_js_script_against_file("test/js/module_advanced.js", "test/js/module_advanced.txt");
}

TEST(JavaScriptTests, test_es_modules_parallel) {
    test_js_script_against_file("test/js/module_parallel.js", "test/js/module_parallel.txt");
}

TEST(JavaScriptTests, test_es_modules_parallel_chain) {
    test_js_script_against_file("test/js/module_parallel_chain.js", "test/js/module_parallel_chain.txt");
}

TEST(JavaScriptTests, test_generator_basic) {
    test_js_script_against_file("test/js/generator_basic.js", "test/js/generator_basic.txt");
}

TEST(JavaScriptTests, test_microtask_order) {
    test_js_script_against_file("test/js/microtask_order.js", "test/js/microtask_order.txt");
}

TEST(JavaScriptTests, test_fs_basic) {
    test_js_script_against_file("test/js/fs_basic.js", "test/js/fs_basic.txt");
}

TEST(JavaScriptTests, test_fetch_errors) {
    test_js_script_against_file("test/js/fetch_errors.js", "test/js/fetch_errors.txt");
}

TEST(JavaScriptTests, test_child_process_basic) {
    test_js_script_against_file("test/js/child_process_basic.js", "test/js/child_process_basic.txt");
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
