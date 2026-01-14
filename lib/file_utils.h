#ifndef FILE_UTILS_H
#define FILE_UTILS_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Create directory recursively (like mkdir -p).
 * Creates all parent directories as needed.
 * 
 * @param path Directory path to create
 * @return 0 on success, -1 on error
 */
int create_dir_recursive(const char* path);

#ifdef __cplusplus
}
#endif

#endif // FILE_UTILS_H
