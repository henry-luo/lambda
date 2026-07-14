#include <gtest/gtest.h>
#include <cstdio>
#include <cstdlib>
#include <cassert>
#include <cstring>
#include <cerrno>

extern "C" {
#include "../lib/shell.h"
}

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

// Execute lambda.exe with one argument and capture combined output.
char* execute_lambda_command(const char* argument) {
    const char* args[] = {LAMBDA_EXE, argument, NULL};
    ShellOptions options = {0};
    options.merge_stderr = true;
    // These smoke tests pass one argv value; shell operators would obscure the actual CLI result.
    ShellResult shell_result = shell_exec(LAMBDA_EXE, args, &options);
    char* output = shell_result.stdout_buf ? strdup(shell_result.stdout_buf) : strdup("");
    shell_result_free(&shell_result);
    return output;
}

// Test basic procedural functionality
TEST(LambdaProcTests, test_lambda_executable_exists) {
    ASSERT_EQ(access(LAMBDA_EXE, F_OK), 0) << "lambda.exe executable not found";
}

TEST(LambdaProcTests, test_lambda_version) {
    char* output = execute_lambda_command("--version");
    if (output) {
        ASSERT_GT(strlen(output), 0) << "Version output should not be empty";
        free(output);
    }
    // Note: If --version is not supported, that's also acceptable
}

TEST(LambdaProcTests, test_lambda_help) {
    char* output = execute_lambda_command("--help");
    if (output) {
        ASSERT_GT(strlen(output), 0) << "Help output should not be empty";
        free(output);
    }
    // Note: If --help is not supported, that's also acceptable
}

TEST(LambdaProcTests, test_lambda_with_nonexistent_file) {
    char* output = execute_lambda_command("nonexistent_file.ls");
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
    char* output = execute_lambda_command("--help");
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


int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
