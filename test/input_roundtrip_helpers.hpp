#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <ctype.h>
#include "../lambda/lambda-data.hpp"
#include "../lib/arraylist.h"
#include "../lib/num_stack.h"
#include "../lib/strbuf.h"
#include "../lib/mem-pool/include/mem_pool.h"
#include "../lib/url.h"

extern "C" {
    Input* input_from_source(char* source, Url* abs_url, String* type, String* flavor);
    Input* input_from_url(String* url, String* type, String* flavor, Url* cwd);
    String* format_data(Item item, String* type, String* flavor, VariableMemPool *pool);
    
    // Use actual URL library functions
    Url* url_parse(const char* input);
    Url* url_parse_with_base(const char* input, const Url* base);
    void url_destroy(Url* url);
}
char* read_text_doc(Url *url);

// Helper function to create a Lambda String from C string
String* create_lambda_string(const char* text);

// Helper function to read file contents
char* read_file_content(const char* filepath);

// Helper function to normalize whitespace for comparison
char* normalize_whitespace(const char* str);

// Helper function to compare JSON strings semantically
bool compare_json_semantically(const char* original, const char* formatted);

// Helper function to compare XML strings semantically
bool compare_xml_semantically(const char* original, const char* formatted);

// Helper function to compare Markdown strings
bool compare_markdown_semantically(const char* original, const char* formatted);

// Helper function to compare Org-mode strings
bool compare_org_semantically(const char* original, const char* formatted);

// Helper function to compare markup strings (unified parser output)
bool compare_markup_semantically(const char* original, const char* formatted);

// Common roundtrip test function
bool test_format_roundtrip(const char* test_file, const char* format_type, const char* test_name);
