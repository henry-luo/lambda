#include <stdio.h>
#include <string.h>
#include "lambda/input/mime-detect.h"

int main() {
    MimeDetector* detector = mime_detector_init();
    FILE* f = fopen("test/input/html_content", "r");
    char content[1000];
    size_t len = fread(content, 1, 999, f);
    content[len] = '\0';
    fclose(f);
    
    printf("HTML content (first 100 chars): %.100s\n", content);
    const char* mime = detect_mime_type(detector, "html_content", content, len);
    printf("HTML content detected as: %s\n", mime ? mime : "NULL");
    
    mime_detector_destroy(detector);
    return 0;
}
