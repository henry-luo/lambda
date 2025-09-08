#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>
#include <stdbool.h>
#include "file.h"
#include "log.h"

// Function to read and display the content of a text file
char* read_text_file(const char *filename) {
    FILE *file = fopen(filename, "r"); // open the file in read mode
    if (file == NULL) { // handle error when file cannot be opened
        log_error("Error opening file: %s", filename);
        return NULL;
    }
    // ensure it is a regular file
    struct stat sb;
    if (fstat(fileno(file), &sb) == -1) {
        log_error("Error getting file status: %s", filename);
        fclose(file);
        return NULL;
    }
    if (!S_ISREG(sb.st_mode)) {
        log_error("Not regular file: %s", filename);
        fclose(file);
        return NULL;
    }

    fseek(file, 0, SEEK_END);  // move the file pointer to the end to determine file size
    long fileSize = ftell(file);
    rewind(file); // reset file pointer to the beginning

    char* buf = (char*)malloc(fileSize + 1); // allocate memory for the file content
    if (!buf) {
        perror("Memory allocation failed");
        fclose(file);
        return NULL;
    }

    // read the file content into the buffer
    size_t bytesRead = fread(buf, 1, fileSize, file);
    buf[bytesRead] = '\0'; // Null-terminate the buffer

    // clean up
    fclose(file);
    return buf;
}

void write_text_file(const char *filename, const char *content) {
    FILE *file = fopen(filename, "w"); // Open the file in write mode
    if (file == NULL) {
        perror("Error opening file"); // Handle error if file cannot be opened
        return;
    }
    // Write the string to the file
    if (fprintf(file, "%s", content) < 0) {
        perror("Error writing to file");
    }
    fclose(file); // Close the file
}

// Create directory recursively if it doesn't exist
bool create_dir(const char* dir_path) {
    struct stat st = {0};
    
    if (stat(dir_path, &st) == 0) {
        return S_ISDIR(st.st_mode);
    }
    
    // Create a mutable copy of the path for manipulation
    char* path_copy = strdup(dir_path);
    if (!path_copy) {
        fprintf(stderr, "Memory allocation failed for path copy\n");
        return false;
    }
    
    // Find the last slash to get parent directory
    char* last_slash = strrchr(path_copy, '/');
    if (last_slash && last_slash != path_copy) {
        *last_slash = '\0';
        
        // Recursively create parent directory
        if (!create_dir(path_copy)) {
            free(path_copy);
            return false;
        }
    }
    
    free(path_copy);
    
    // Now create this directory
    if (mkdir(dir_path, 0755) == -1) {
        fprintf(stderr, "Failed to create directory %s: %s\n", 
                dir_path, strerror(errno));
        return false;
    }
    
    return true;
}
