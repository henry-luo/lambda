// input_cache_util.cpp
// Utility for cache directory management and config loading

#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>

// Ensure cache directory exists, create if needed
int ensure_cache_directory(const char* cache_dir) {
    struct stat st;
    if (stat(cache_dir, &st) == 0) {
        if (S_ISDIR(st.st_mode)) return 0; // Exists
        fprintf(stderr, "Path exists but is not a directory: %s\n", cache_dir);
        return -1;
    }
    // Try to create
    if (mkdir(cache_dir, 0755) == 0) return 0;
    fprintf(stderr, "Failed to create cache directory %s: %s\n", cache_dir, strerror(errno));
    return -1;
}
