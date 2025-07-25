#include <stdio.h>
#include <string.h>
#include "lambda/input/mime-detect.h"

int main() {
    const char* test_content = "<!DOCTYPE html>";
    printf("Test string: '%s' (len: %zu)\n", test_content, strlen(test_content));
    printf("Pattern: '<!DOCTYPE html' (len: 15)\n");
    printf("memcmp result: %d\n", memcmp(test_content, "<!DOCTYPE html", 15));
    
    MimeDetector* detector = mime_detector_init();
    const char* mime = detect_mime_from_content(detector, test_content, strlen(test_content));
    printf("Detected MIME: %s\n", mime ? mime : "NULL");
    
    mime_detector_destroy(detector);
    return 0;
}
