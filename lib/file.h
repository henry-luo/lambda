#ifndef FILE_H
#define FILE_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Function to read and display the content of a text file
char* read_text_file(const char *filename);

// Function to read binary file with explicit size output
// Returns allocated buffer, sets out_size to number of bytes read
// Caller must free() the returned buffer
char* read_binary_file(const char *filename, size_t *out_size);

// Function to write content to a text file
void write_text_file(const char *filename, const char *content);

// Function to create directory recursively if it doesn't exist
bool create_dir(const char* dir_path);

#ifdef __cplusplus
}
#endif

#endif // FILE_H
