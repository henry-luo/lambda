// input_dir.cpp
// Directory handling for Lambda Script input system
// Implements directory listing and element generation

#include "input.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <time.h>


#include <stdlib.h>
#include <sys/stat.h>

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
static Element* create_entry_element(Input* input, const char* name, const char* path, struct stat* st, int is_dir) {
    Element* elmt = input_create_element(input, is_dir ? "dir" : "file");
    if (!elmt) return NULL;
    input_add_attribute_to_element(input, elmt, "name", name);
    char buf[64];
    snprintf(buf, sizeof(buf), "%ld", (long)st->st_size);
    input_add_attribute_to_element(input, elmt, "size", buf);
    snprintf(buf, sizeof(buf), "%ld", (long)st->st_mtime);
    input_add_attribute_to_element(input, elmt, "modified", buf);
    snprintf(buf, sizeof(buf), "%o", (unsigned int)(st->st_mode & 0777));
    input_add_attribute_to_element(input, elmt, "mode", buf);
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
        struct stat st;
        if (stat(full_path, &st) != 0) continue;
        int is_dir = S_ISDIR(st.st_mode);
        Element* elmt = create_entry_element(input, entry->d_name, full_path, &st, is_dir);
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
    
    Element* root = create_entry_element(input, dir_name, directory_path, &st, 1);
    if (!root) { free(input); return NULL; }
    
    // Add the full path as a separate attribute for the root directory
    input_add_attribute_to_element(input, root, "path", directory_path);
    
    // Traverse and populate
    traverse_directory(input, root, directory_path, recursive, max_depth, 0);
    input->root = {.item = (uint64_t)root};
    return input;
}
