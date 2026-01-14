// file_utils.c
// File system utility functions

#include "file_utils.h"
#include <sys/stat.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#ifdef _WIN32
#include <direct.h>
#define mkdir(path, mode) _mkdir(path)
#else
#include <sys/types.h>
#endif

// Create directory recursively (like mkdir -p)
int create_dir_recursive(const char* path) {
    if (!path || !*path) {
        return -1;
    }

    char* path_copy = strdup(path);
    if (!path_copy) {
        return -1;
    }

    char* p = path_copy;
    struct stat st;  // Declare stat buffer here
    
    // Skip leading slash
    if (*p == '/') {
        p++;
    }

    for (; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            
            // Check if directory exists
            if (stat(path_copy, &st) != 0) {
                // Create directory
                if (mkdir(path_copy, 0755) != 0 && errno != EEXIST) {
                    free(path_copy);
                    return -1;
                }
            }
            
            *p = '/';
        }
    }

    // Create final directory
    if (stat(path_copy, &st) != 0) {
        if (mkdir(path_copy, 0755) != 0 && errno != EEXIST) {
            free(path_copy);
            return -1;
        }
    }

    free(path_copy);
    return 0;
}
