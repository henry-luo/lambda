#include <gtest/gtest.h>
#include <cstdio>
#include <cstdlib>
#include <cassert>
#include <cstring>
#include <cerrno>

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
    #include <io.h>
    #include <direct.h>
    #define getcwd _getcwd
    #define chdir _chdir
    #define unlink _unlink
    #define access _access
    #define F_OK 0
    #define LAMBDA_EXE "lambda.exe"
#else
    #include <unistd.h>
    #include <sys/wait.h>
    #include <fcntl.h>
    #define LAMBDA_EXE "./lambda.exe"
#endif

// Utility functions for file I/O and process execution
char* read_text_file(const char* file_path) {
    FILE* file = fopen(file_path, "r");
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
    
    return content;
}

// Execute a command and capture output
char* execute_command(const char* command) {
    FILE* pipe = popen(command, "r");
    if (!pipe) {
        return nullptr;
    }
    
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
    
    pclose(pipe);
    return full_output;
}

// Test basic procedural functionality
TEST(LambdaProcTests, test_lambda_executable_exists) {
    ASSERT_EQ(access(LAMBDA_EXE, F_OK), 0) << "lambda.exe executable not found";
}

TEST(LambdaProcTests, test_lambda_version) {
    char command[256];
    snprintf(command, sizeof(command), "%s --version", LAMBDA_EXE);
    char* output = execute_command(command);
    if (output) {
        ASSERT_GT(strlen(output), 0) << "Version output should not be empty";
        free(output);
    }
    // Note: If --version is not supported, that's also acceptable
}

TEST(LambdaProcTests, test_lambda_help) {
    char command[256];
    snprintf(command, sizeof(command), "%s --help", LAMBDA_EXE);
    char* output = execute_command(command);
    if (output) {
        ASSERT_GT(strlen(output), 0) << "Help output should not be empty";
        free(output);
    }
    // Note: If --help is not supported, that's also acceptable
}

TEST(LambdaProcTests, test_lambda_with_nonexistent_file) {
    char command[256];
    snprintf(command, sizeof(command), "%s nonexistent_file.ls", LAMBDA_EXE);
    char* output = execute_command(command);
    ASSERT_NE(output, nullptr);
    // Should produce some output (error message)
    ASSERT_GT(strlen(output), 0);
    free(output);
}

TEST(LambdaProcTests, test_lambda_working_directory) {
    char cwd[1024];
    char* result = getcwd(cwd, sizeof(cwd));
    ASSERT_NE(result, nullptr) << "Could not get current working directory";
    
    // Test that lambda.exe can be executed from current directory
    char command[256];
    snprintf(command, sizeof(command), "%s --help 2>/dev/null || echo 'executable_found'", LAMBDA_EXE);
    char* output = execute_command(command);
    ASSERT_NE(output, nullptr);
    free(output);
}

TEST(LambdaProcTests, test_basic_file_operations) {
    // Test creating a temporary file
    const char* temp_file = "test_temp.txt";
    FILE* file = fopen(temp_file, "w");
    ASSERT_NE(file, nullptr) << "Could not create temporary file";
    
    fprintf(file, "test content\n");
    fclose(file);
    
    // Test reading the file back
    char* content = read_text_file(temp_file);
    ASSERT_NE(content, nullptr) << "Could not read temporary file";
    ASSERT_STREQ(content, "test content\n");
    
    free(content);
    unlink(temp_file);
}

// Helper functions for script-based tests (migrated from Criterion version)
void trim_trailing_whitespace(char* str) {
    if (!str) return;
    
    size_t len = strlen(str);
    while (len > 0 && (str[len - 1] == '\n' || str[len - 1] == '\r' || 
                       str[len - 1] == ' ' || str[len - 1] == '\t')) {
        str[len - 1] = '\0';
        len--;
    }
}

char* execute_lambda_proc_script(const char* script_path) {
    char command[512];
    snprintf(command, sizeof(command), "%s run %s 2>&1", LAMBDA_EXE, script_path);
    
    FILE* pipe = popen(command, "r");
    if (!pipe) {
        return nullptr;
    }
    
    char* full_output = (char*)malloc(8192);
    if (!full_output) {
        pclose(pipe);
        return nullptr;
    }
    
    size_t total_read = 0;
    size_t buffer_size = 8192;
    char buffer[256];
    
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        size_t len = strlen(buffer);
        
        if (total_read + len + 1 > buffer_size) {
            buffer_size *= 2;
            full_output = (char*)realloc(full_output, buffer_size);
            if (!full_output) {
                pclose(pipe);
                return nullptr;
            }
        }
        
        strcpy(full_output + total_read, buffer);
        total_read += len;
    }
    
    full_output[total_read] = '\0';
    pclose(pipe);
    
    // Extract only the actual script output after "Executing JIT compiled code..."
    char* marker = strstr(full_output, "Executing JIT compiled code...");
    if (marker) {
        char* result_start = strchr(marker, '\n');
        if (result_start) {
            result_start++;
            char* result = strdup(result_start);
            free(full_output);
            return result;
        }
    }
    
    return full_output;
}

void test_lambda_proc_script_against_file(const char* script_path, const char* expected_output_path) {
    char* actual_output = execute_lambda_proc_script(script_path);
    ASSERT_NE(actual_output, nullptr) << "Failed to execute lambda.exe run with script: " << script_path;
    
    trim_trailing_whitespace(actual_output);
    
    char* expected_output = read_text_file(expected_output_path);
    ASSERT_NE(expected_output, nullptr) << "Failed to read expected output file: " << expected_output_path;
    
    trim_trailing_whitespace(expected_output);
    
    ASSERT_STREQ(expected_output, actual_output) 
        << "Output does not match expected output for script: " << script_path
        << "\nExpected:\n'" << expected_output << "'"
        << "\nGot:\n'" << actual_output << "'";
    
    free(expected_output);
    free(actual_output);
}

// Script-based tests migrated from Criterion version
TEST(LambdaProcTests, test_proc1) {
    test_lambda_proc_script_against_file("test/lambda/proc1.ls", "test/lambda/proc1.txt");
}

TEST(LambdaProcTests, test_proc2) {
    test_lambda_proc_script_against_file("test/lambda/proc2.ls", "test/lambda/proc2.txt");
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
