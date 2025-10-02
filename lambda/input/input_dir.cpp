// input_dir.cpp
// Directory handling for Lambda Script input system
// Implements directory listing and element generation

#include "input.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#ifndef _WIN32
#include <unistd.h>
#endif
#include <string.h>
#include <errno.h>
#include <time.h>
#include <stdlib.h>
#include "../../lib/datetime.h"
#include <sys/stat.h>

// Windows compatibility definitions
#ifdef _WIN32
#define lstat stat
#define S_ISLNK(mode) (0)  // Windows doesn't have symbolic links in the same way
#endif

// Helper: check if path is a directory
static int is_directory(const char* path) {
    struct stat st;
    if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) return 1;
    return 0;
}


#include <dirent.h>
#include <sys/types.h>
#include <time.h>

// Helper: create <file> or <dir> element with metadata
static Element* create_entry_element(Input* input, const char* name, const char* path, struct stat* st, int is_dir, bool is_link) {
    Element* elmt = input_create_element(input, is_dir ? "dir" : "file");
    if (!elmt) return NULL;
    input_add_attribute_to_element(input, elmt, "name", name);

    // Create size as integer
    int64_t* size_ptr;
    size_ptr = (int64_t*)pool_calloc(input->pool, sizeof(int64_t));
    if (size_ptr != NULL) {
        *size_ptr = (int64_t)st->st_size;
        Item size_item = {.item = l2it(size_ptr)};
        input_add_attribute_item_to_element(input, elmt, "size", size_item);
    }

    // Create modified as Lambda datetime from Unix timestamp
    DateTime* dt_ptr = datetime_from_unix(input->pool, (int64_t)st->st_mtime);
    if (dt_ptr) {
        Item datetime_item = {.item = k2it(dt_ptr)};
        input_add_attribute_item_to_element(input, elmt, "modified", datetime_item);
    }

    // Add is_link attribute if it's a symbolic link
    if (is_link) {
        Item link_item = {.item = b2it(true)};
        input_add_attribute_item_to_element(input, elmt, "is_link", link_item);
    }

    // Keep mode as string for permissions
    char buf[64];
    snprintf(buf, sizeof(buf), "%o", (unsigned int)(st->st_mode & 0777));
    String* mode_str = input_create_string(input, buf);
    input_add_attribute_item_to_element(input, elmt, "mode", {.item = y2it(mode_str)});
    return elmt;
}

// Recursive directory traversal
static void traverse_directory(Input* input, Element* parent, const char* dir_path, bool recursive, int max_depth, int cur_depth) {
    DIR* dir = opendir(dir_path);
    if (!dir) return;
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        char full_path[4096];
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);

        // Use lstat to detect symbolic links
        struct stat lst;
        if (lstat(full_path, &lst) != 0) continue;
        bool is_link = S_ISLNK(lst.st_mode);

        // Use stat for target information (follows links)
        struct stat st;
        if (stat(full_path, &st) != 0) {
            // If stat fails but lstat succeeded, it's a broken symlink
            // Use lstat data for broken symlinks
            st = lst;
        }

        int is_dir = S_ISDIR(st.st_mode);
        Element* elmt = create_entry_element(input, entry->d_name, full_path, &st, is_dir, is_link);
        if (!elmt) continue;
        // Add as child content, not attribute
        Item elmt_item = {0};
        elmt_item.type_id = LMD_TYPE_ELEMENT;
        elmt_item.element = elmt;
        list_push((List*)parent, elmt_item);
        ((TypeElmt*)parent->type)->content_length++;
        // Recurse if directory
        if (recursive && is_dir && (max_depth < 0 || cur_depth < max_depth)) {
            traverse_directory(input, elmt, full_path, recursive, max_depth, cur_depth + 1);
        }
    }
    closedir(dir);
}

// Returns an Input* representing the directory contents as <dir> element
Input* input_from_directory(const char* directory_path, bool recursive, int max_depth) {
    if (!is_directory(directory_path)) {
        fprintf(stderr, "Not a directory: %s\n", directory_path);
        return NULL;
    }
    // Create Input and root <dir> element
    Input* input = input_new(NULL);
    struct stat st;
    if (stat(directory_path, &st) != 0) return NULL;

    // Extract just the directory name from the full path
    const char* dir_name = strrchr(directory_path, '/');
    if (dir_name) {
        dir_name++; // Skip the '/'
    } else {
        dir_name = directory_path; // No '/' found, use the whole path
    }

    Element* root = create_entry_element(input, dir_name, directory_path, &st, 1, false);
    if (!root) { free(input); return NULL; }

    // Add the full path as a separate attribute for the root directory
    input_add_attribute_to_element(input, root, "path", directory_path);

    // Traverse and populate
    traverse_directory(input, root, directory_path, recursive, max_depth, 0);
    input->root = {.item = (uint64_t)root};
    return input;
}
