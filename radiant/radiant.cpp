#include <stdio.h>
#include <string.h>
#include "view.hpp"

int run_layout(const char* html_file);
int window_main(int argc, char* argv[]);

int main(int argc, char* argv[]) {
    // Check for layout sub-command
    if (argc >= 3 && strcmp(argv[1], "layout") == 0) {
        return run_layout(argv[2]);
    }

    // Check for help
    if (argc >= 2 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        printf("Radiant HTML/CSS Layout Engine\n");
        printf("Usage:\n");
        printf("  %s                    # Run GUI mode (default)\n", argv[0]);
        printf("  %s layout <file.html> # Run layout test on HTML file\n", argv[0]);
        printf("  %s --help            # Show this help\n", argv[0]);
        return 0;
    }
    // Original GUI mode
    return window_main(argc, argv);
}
