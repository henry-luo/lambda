// input_cache_util.cpp
// Utility for cache directory management and config loading

#include "../../lib/file.h"

// Ensure cache directory exists, create if needed
int ensure_cache_directory(const char* cache_dir) {
    return file_ensure_dir(cache_dir);
}
