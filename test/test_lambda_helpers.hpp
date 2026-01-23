#ifndef TEST_LAMBDA_HELPERS_HPP
#define TEST_LAMBDA_HELPERS_HPP

#include <gtest/gtest.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>

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
    #define F_OK 0
    #define LAMBDA_EXE "lambda.exe"
#else
    #include <unistd.h>
    #include <sys/wait.h>
    #include <dirent.h>
    #include <sys/stat.h>
    #define LAMBDA_EXE "./lambda.exe"
#endif

// Structure to hold test script info
struct LambdaTestInfo {
    std::string script_path;
    std::string expected_path;
    std::string test_name;
    bool is_procedural;  // true for procedural scripts (run with "lambda.exe run")
    
    // For Google Test parameterized test naming
    friend std::ostream& operator<<(std::ostream& os, const LambdaTestInfo& info) {
        return os << info.test_name;
    }
};

// Helper function to execute a lambda script and capture output
// is_procedural: if true, uses "./lambda.exe run <script>" for procedural scripts
inline char* execute_lambda_script(const char* script_path, bool is_procedural = false) {
    char command[512];
#ifdef _WIN32
    if (is_procedural) {
        snprintf(command, sizeof(command), "lambda.exe run \"%s\"", script_path);
    } else {
        snprintf(command, sizeof(command), "lambda.exe \"%s\"", script_path);
    }
#else
    if (is_procedural) {
        snprintf(command, sizeof(command), "./lambda.exe run \"%s\"", script_path);
    } else {
        snprintf(command, sizeof(command), "./lambda.exe \"%s\"", script_path);
    }
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
        fprintf(stderr, "Error: lambda.exe exited with code %d for script: %s\n",
                WEXITSTATUS(exit_code), script_path);
        free(full_output);
        return nullptr;
    }

    // Extract result from "##### Script" marker
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
inline void trim_trailing_whitespace(char* str) {
    if (!str) return;
    size_t len = strlen(str);
    while (len > 0 && isspace((unsigned char)str[len - 1])) {
        str[--len] = '\0';
    }
}

// Helper function to check if a file exists
inline bool file_exists(const std::string& path) {
    FILE* f = fopen(path.c_str(), "r");
    if (f) {
        fclose(f);
        return true;
    }
    return false;
}

// Helper function to get test name from script path
inline std::string get_test_name(const std::string& script_path) {
    // Extract filename without extension
    size_t last_slash = script_path.find_last_of("/\\");
    std::string filename = (last_slash != std::string::npos) 
        ? script_path.substr(last_slash + 1) 
        : script_path;
    
    size_t dot_pos = filename.find_last_of('.');
    if (dot_pos != std::string::npos) {
        filename = filename.substr(0, dot_pos);
    }
    
    // Replace invalid characters for test names
    for (char& c : filename) {
        if (!isalnum(c) && c != '_') {
            c = '_';
        }
    }
    
    return filename;
}

// Discover all .ls files with matching .txt files in a directory
inline std::vector<LambdaTestInfo> discover_tests_in_directory(const char* dir_path, bool is_procedural = false) {
    std::vector<LambdaTestInfo> tests;
    
#ifdef _WIN32
    std::string search_path = std::string(dir_path) + "\\*.ls";
    WIN32_FIND_DATAA find_data;
    HANDLE find_handle = FindFirstFileA(search_path.c_str(), &find_data);
    
    if (find_handle != INVALID_HANDLE_VALUE) {
        do {
            std::string filename = find_data.cFileName;
            std::string script_path = std::string(dir_path) + "/" + filename;
            
            // Build expected output path (.ls -> .txt)
            std::string expected_path = script_path;
            size_t dot_pos = expected_path.find_last_of('.');
            if (dot_pos != std::string::npos) {
                expected_path = expected_path.substr(0, dot_pos) + ".txt";
            }
            
            // Only add if matching .txt file exists
            if (file_exists(expected_path)) {
                LambdaTestInfo info;
                info.script_path = script_path;
                info.expected_path = expected_path;
                info.test_name = get_test_name(script_path);
                info.is_procedural = is_procedural;
                tests.push_back(info);
            }
        } while (FindNextFileA(find_handle, &find_data));
        FindClose(find_handle);
    }
#else
    DIR* dir = opendir(dir_path);
    if (!dir) {
        return tests;
    }
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string filename = entry->d_name;
        
        // Check if it's a .ls file
        if (filename.length() > 3 && filename.substr(filename.length() - 3) == ".ls") {
            std::string script_path = std::string(dir_path) + "/" + filename;
            
            // Build expected output path (.ls -> .txt)
            std::string expected_path = script_path;
            size_t dot_pos = expected_path.find_last_of('.');
            if (dot_pos != std::string::npos) {
                expected_path = expected_path.substr(0, dot_pos) + ".txt";
            }
            
            // Only add if matching .txt file exists
            if (file_exists(expected_path)) {
                LambdaTestInfo info;
                info.script_path = script_path;
                info.expected_path = expected_path;
                info.test_name = get_test_name(script_path);
                info.is_procedural = is_procedural;
                tests.push_back(info);
            }
        }
    }
    closedir(dir);
#endif
    
    // Sort tests by name for consistent ordering
    std::sort(tests.begin(), tests.end(), [](const LambdaTestInfo& a, const LambdaTestInfo& b) {
        return a.test_name < b.test_name;
    });
    
    return tests;
}

// Helper function to read expected output from file
inline char* read_expected_output(const char* expected_file_path) {
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

// Helper function to test lambda script against expected output file
inline void test_lambda_script_against_file(const char* script_path, const char* expected_file_path, bool is_procedural) {
    // Get script name for better error messages
    const char* script_name = strrchr(script_path, '/');
    script_name = script_name ? script_name + 1 : script_path;

    char* expected_output = read_expected_output(expected_file_path);
    ASSERT_NE(expected_output, nullptr) << "Could not read expected output file: " << expected_file_path;

    char* actual_output = execute_lambda_script(script_path, is_procedural);
    ASSERT_NE(actual_output, nullptr) << "Could not execute lambda script: " << script_path;

    // Trim whitespace from actual output
    trim_trailing_whitespace(actual_output);

    // Compare outputs
    ASSERT_STREQ(expected_output, actual_output)
        << "Output mismatch for script: " << script_path
        << " (expected " << strlen(expected_output) << " chars, got " << strlen(actual_output) << " chars)";

    free(expected_output);
    free(actual_output);
}

#endif // TEST_LAMBDA_HELPERS_HPP
