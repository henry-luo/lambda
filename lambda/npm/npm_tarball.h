#ifndef NPM_TARBALL_H
#define NPM_TARBALL_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Extract a .tgz (gzipped tar) file to a destination directory.
// npm tarballs have a "package/" prefix on all entries which is stripped.
// Returns 0 on success, -1 on error.
int npm_extract_tarball(const char* tgz_path, const char* dest_dir);

#ifdef __cplusplus
}
#endif

#endif // NPM_TARBALL_H
