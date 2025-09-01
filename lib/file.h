#ifndef FILE_H
#define FILE_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Function to read and display the content of a text file
char* read_text_file(const char *filename);

// Function to write content to a text file
void write_text_file(const char *filename, const char *content);

// Function to create directory recursively if it doesn't exist
bool create_dir(const char* dir_path);

#ifdef __cplusplus
}
#endif

#endif // FILE_H
