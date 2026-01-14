#pragma once
#ifndef LAMBDA_INPUT_H
#define LAMBDA_INPUT_H

#define LAMBDA_STATIC
#include "../lambda-data.hpp"
#include "../../lib/url.h"
#include "../../lib/log.h"

// InputManager - manages global pool and input lifecycle
class InputManager {
private:
    Pool* global_pool;
    ArrayList* inputs;              // track all created inputs for cleanup
    mpd_context_t* decimal_ctx;     // libmpdec context for decimal operations

    // Private constructor for singleton pattern
    InputManager();
    ~InputManager();

    // Delete copy constructor and assignment operator
    InputManager(const InputManager&) = delete;
    InputManager& operator=(const InputManager&) = delete;

public:
    static mpd_context_t* decimal_context();

    // Create a new input using the managed pool
    static Input* create_input(Url* abs_url);

    // Destroy the global instance (optional cleanup)
    static void destroy_global();

    // Instance methods for direct manager usage
    Input* create_input_instance(Url* abs_url);
    Pool* get_pool() const { return global_pool; }
};

#include "../mark_builder.hpp"

#ifdef __cplusplus
extern "C" {
#endif
// Common input utility functions
void skip_whitespace(const char **text);    // skip spaces, tabs, newlines
void skip_tab_pace(const char **text);      // skip spaces and tabs only
bool input_is_whitespace_char(char c);
bool input_is_empty_line(const char* line);
int input_count_leading_chars(const char* str, char ch);
char* input_trim_whitespace(const char* str);
char** input_split_lines(const char* text, int* line_count);
void input_free_lines(char** lines, int line_count);
Input* input_from_source(const char* source, Url* url, String* type, String* flavor);
Input* input_from_directory(const char* directory_path, bool recursive, int max_depth);
Input* input_from_url(String* url, String* type, String* flavor, Url* cwd);

// Math parsing functions (from input-math.cpp)
void parse_math(Input* input, const char* math_string, const char* flavor_str);
void cleanup_math_parser();  // Call at program exit to free resources

#ifdef __cplusplus
} // extern "C"
extern "C" {
#endif

// ASCII Math parsing functions (from input-math-ascii.cpp)
Item input_ascii_math(Input* input, const char* ascii_math);

// Unified markup parsing functions (from input-markup.cpp)
Item input_markup(Input *input, const char* content);

// JSX parsing functions (from input-jsx.cpp)
Item input_jsx(Input* input, const char* jsx_string);
void parse_jsx(Input* input, const char* jsx_string);

// MDX parsing functions (from input-mdx.cpp)
Item input_mdx(Input* input, const char* mdx_string);

// Directory listing functions (from input_dir.cpp)
Input* input_from_directory(const char* directory_path, bool recursive, int max_depth);

// HTTP/HTTPS functions (from input_http.cpp)
typedef struct {
    long timeout_seconds;
    long max_redirects;
    const char* user_agent;
    bool verify_ssl;
    bool enable_compression;
} HttpConfig;

// Extended fetch configuration and response structures
typedef struct {
    const char* method;
    const char* body;
    size_t body_size;
    char** headers;
    int header_count;
    long timeout_seconds;
    long max_redirects;
    const char* user_agent;
    bool verify_ssl;
    bool enable_compression;
} FetchConfig;

typedef struct FetchResponse {
    char* data;
    size_t size;
    long status_code;
    char** response_headers;
    int response_header_count;
    char* content_type;
} FetchResponse;

char* download_http_content(const char* url, size_t* content_size, const HttpConfig* config);
char* download_to_cache(const char* url, const char* cache_dir, char** out_cache_path);
Input* input_from_http(const char* url, const char* type, const char* flavor, const char* cache_dir);
FetchResponse* http_fetch(const char* url, const FetchConfig* config);
void free_fetch_response(FetchResponse* response);
const char* content_type_to_extension(const char* content_type);

// System information functions (from input_sysinfo.cpp)
typedef struct SysInfoManager SysInfoManager;

SysInfoManager* sysinfo_manager_create(void);
void sysinfo_manager_destroy(SysInfoManager* manager);
Input* input_from_sysinfo(Url* url, Pool* pool);
bool is_sys_url(const char* url);

// Graph parsing functions (from input-graph.cpp)
void parse_graph(Input* input, const char* graph_string, const char* flavor);
void parse_graph_dot(Input* input, const char* dot_string);
void parse_graph_mermaid(Input* input, const char* mermaid_string);

// HTML element extraction functions (from input.cpp)
// Get the <html> element from #document tree built by HTML5 parser
Element* input_get_html_element(Input* input);

// Get fragment element - extracts single element from body for fragments,
// or returns <html> element for full documents
Element* input_get_html_fragment_element(Input* input, const char* original_html);

#ifdef __cplusplus
}
#endif

#endif // LAMBDA_INPUT_H
