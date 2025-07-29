#ifndef PAGE_OUTPUT_H
#define PAGE_OUTPUT_H

#include "../typeset.h"

// Individual page output structure
struct PageOutput {
    int page_number;            // Page number (1-based)
    String* svg_content;        // SVG content for this page
    float width;                // Page width in points
    float height;               // Page height in points
    struct PageOutput* next;    // Next page in sequence
    struct PageOutput* prev;    // Previous page in sequence
    
    // Page metadata
    char* title;                // Page title
    char* description;          // Page description
    
    // Layout information
    float content_width;        // Actual content width
    float content_height;       // Actual content height
    int element_count;          // Number of rendered elements
    
    // File information
    char* filename;             // Generated filename
    size_t file_size;          // SVG file size in bytes
    
    // Rendering statistics  
    float render_time;          // Time taken to render this page
    int text_elements;          // Number of text elements
    int math_elements;          // Number of math elements
    int graphic_elements;       // Number of graphic elements
};

// Complete document output structure
struct DocumentOutput {
    PageOutput* first_page;     // First page in document
    PageOutput* last_page;      // Last page in document
    int total_pages;            // Total number of pages
    Document* source_document;  // Source document reference
    
    // Document metadata
    char* document_title;       // Document title
    char* document_author;      // Document author
    char* document_subject;     // Document subject
    char* creation_date;        // Creation timestamp
    
    // Output settings
    char* base_filename;        // Base filename for pages
    char* output_directory;     // Output directory path
    
    // Rendering statistics
    float total_render_time;    // Total rendering time
    size_t total_file_size;     // Total size of all SVG files
    int total_elements;         // Total rendered elements
    
    // Error tracking
    int error_count;            // Number of rendering errors
    char** error_messages;      // Array of error messages
};

// Output format options
typedef struct OutputOptions {
    // File naming
    char* filename_pattern;     // Pattern for page filenames
    bool zero_pad_numbers;      // Zero-pad page numbers
    int number_width;           // Width for number padding
    
    // SVG options
    bool optimize_svg;          // Optimize SVG output
    bool embed_fonts;           // Embed font data
    bool use_css_styles;        // Use CSS for styling
    int decimal_precision;      // Decimal precision for coordinates
    
    // Compression
    bool compress_output;       // Compress SVG files
    float compression_level;    // Compression level (0-1)
    
    // Metadata
    bool include_metadata;      // Include document metadata
    bool include_timestamps;    // Include creation timestamps
    bool include_statistics;    // Include rendering statistics
    
    // Debug options
    bool include_debug_info;    // Include debug information
    bool show_bounding_boxes;   // Show element bounding boxes
    bool show_baselines;        // Show text baselines
} OutputOptions;

// Core output functions
DocumentOutput* render_document_to_svg_pages(TypesetEngine* engine, Document* doc);
DocumentOutput* render_document_with_options(TypesetEngine* engine, Document* doc, OutputOptions* options);
void save_document_pages(DocumentOutput* output, const char* base_filename);
void save_document_pages_to_directory(DocumentOutput* output, const char* directory);

// Page output management
PageOutput* page_output_create(int page_number, float width, float height);
void page_output_destroy(PageOutput* page);
void page_output_set_svg_content(PageOutput* page, String* svg_content);
void page_output_set_metadata(PageOutput* page, const char* title, const char* description);

// Document output management
DocumentOutput* document_output_create(Document* source_doc);
void document_output_destroy(DocumentOutput* output);
void document_output_add_page(DocumentOutput* output, PageOutput* page);
void document_output_remove_page(DocumentOutput* output, PageOutput* page);
PageOutput* document_output_get_page(DocumentOutput* output, int page_number);

// File operations
void save_page_to_file(PageOutput* page, const char* filename);
void save_page_to_stream(PageOutput* page, FILE* stream);
bool page_output_write_to_buffer(PageOutput* page, char* buffer, size_t buffer_size);

// Filename generation
char* generate_page_filename(const char* base_filename, int page_number, OutputOptions* options);
char* generate_default_filename(Document* doc, int page_number);
void ensure_output_directory_exists(const char* directory);

// Output options management
OutputOptions* output_options_create(void);
void output_options_destroy(OutputOptions* options);
OutputOptions* output_options_create_default(void);
OutputOptions* output_options_create_web_optimized(void);
OutputOptions* output_options_create_print_optimized(void);

// Output validation
bool validate_document_output(DocumentOutput* output);
bool validate_page_output(PageOutput* page);
void fix_invalid_svg_content(PageOutput* page);

// Batch processing
DocumentOutput** render_multiple_documents(TypesetEngine* engine, Document** docs, int doc_count, OutputOptions* options);
void save_multiple_documents(DocumentOutput** outputs, int count, const char* base_directory);
void destroy_multiple_outputs(DocumentOutput** outputs, int count);

// Page manipulation
PageOutput* page_output_copy(PageOutput* source);
void page_output_merge(PageOutput* target, PageOutput* source);
DocumentOutput* document_output_extract_pages(DocumentOutput* source, int start_page, int end_page);
DocumentOutput* document_output_merge(DocumentOutput* doc1, DocumentOutput* doc2);

// Statistics and analysis
void calculate_output_statistics(DocumentOutput* output);
void print_output_statistics(DocumentOutput* output);
void export_statistics_to_json(DocumentOutput* output, const char* filename);

// Metadata management
void document_output_set_metadata(DocumentOutput* output, const char* title, const char* author, const char* subject);
void document_output_add_custom_metadata(DocumentOutput* output, const char* key, const char* value);
void page_output_add_custom_metadata(PageOutput* page, const char* key, const char* value);

// SVG post-processing
void optimize_svg_pages(DocumentOutput* output);
void compress_svg_pages(DocumentOutput* output, float compression_level);
void embed_fonts_in_pages(DocumentOutput* output, FontManager* font_manager);
void apply_css_styles_to_pages(DocumentOutput* output, const char* css_content);

// Error handling and reporting
void document_output_add_error(DocumentOutput* output, const char* error_message);
void page_output_add_error(PageOutput* page, const char* error_message);
char** document_output_get_errors(DocumentOutput* output, int* error_count);
void document_output_clear_errors(DocumentOutput* output);

// Progress monitoring
typedef struct RenderProgress {
    int current_page;
    int total_pages;
    float percentage_complete;
    float estimated_time_remaining;
    char* current_operation;
} RenderProgress;

typedef void (*ProgressCallback)(RenderProgress* progress, void* user_data);

void document_output_set_progress_callback(ProgressCallback callback, void* user_data);
void update_render_progress(int current_page, int total_pages, const char* operation);

// Template and styling
void apply_page_template(PageOutput* page, const char* template_svg);
void apply_document_styles(DocumentOutput* output, const char* css_styles);
void set_page_background(PageOutput* page, Color background_color);
void add_page_watermark(PageOutput* page, const char* watermark_text, float opacity);

// Export formats
bool export_pages_as_png(DocumentOutput* output, const char* base_filename, float dpi);
bool export_pages_as_pdf(DocumentOutput* output, const char* filename);
bool export_pages_as_html(DocumentOutput* output, const char* filename);
bool export_pages_as_zip(DocumentOutput* output, const char* filename);

// Utility functions
size_t calculate_total_output_size(DocumentOutput* output);
float calculate_average_page_size(DocumentOutput* output);
int count_total_elements(DocumentOutput* output);
char* get_output_summary(DocumentOutput* output);

// Page iteration
typedef void (*PageIteratorCallback)(PageOutput* page, void* user_data);
void document_output_foreach_page(DocumentOutput* output, PageIteratorCallback callback, void* user_data);

// Memory management optimization
void document_output_compress_memory(DocumentOutput* output);
void document_output_free_unused_memory(DocumentOutput* output);
size_t document_output_get_memory_usage(DocumentOutput* output);

// Comparison and diff
typedef struct PageDiff {
    int page_number;
    bool pages_identical;
    float similarity_score;
    char* differences_description;
} PageDiff;

PageDiff* compare_page_outputs(PageOutput* page1, PageOutput* page2);
DocumentOutput* create_diff_document(DocumentOutput* doc1, DocumentOutput* doc2);
void page_diff_destroy(PageDiff* diff);

// Conversion utilities
String* page_output_to_base64(PageOutput* page);
PageOutput* page_output_from_base64(const char* base64_data);
char* page_output_to_data_uri(PageOutput* page);

// Quality assurance
bool validate_svg_syntax(PageOutput* page);
void fix_svg_syntax_errors(PageOutput* page);
float calculate_page_quality_score(PageOutput* page);
void generate_quality_report(DocumentOutput* output, const char* report_filename);

#endif // PAGE_OUTPUT_H
