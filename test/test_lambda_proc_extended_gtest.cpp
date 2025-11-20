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

// Helper functions for script-based tests
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

// Extended test that requires network access
TEST(LambdaProcExtendedTests, test_proc_fetch) {
    test_lambda_proc_script_against_file("test/lambda/proc_fetch.ls", "test/lambda/proc_fetch.txt");
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
