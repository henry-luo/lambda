#ifndef RENDER_PDF_HPP
#define RENDER_PDF_HPP

// Main function to render HTML to PDF
int render_html_to_pdf(const char* html_file, const char* pdf_file,
                       int viewport_width, int viewport_height,
                       float scale = 1.0f);

#endif // RENDER_PDF_HPP
