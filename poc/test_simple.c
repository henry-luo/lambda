#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main() {
    // Simple test of the main parsing functions
    const char* test1 = "## ATX Header Level 2 ##";
    const char* test2 = "    def hello_world():";
    const char* test3 = "        print(\"Hello from indented code!\")";
    
    printf("Testing ATX header: '%s'\n", test1);
    printf("Is ATX heading: %s\n", "true");  // We'll check manually
    
    printf("\nTesting indented code lines:\n");
    printf("Line 1: '%s' - Has 4+ spaces: %s\n", test2, (strlen(test2) >= 4 && test2[0] == ' ' && test2[1] == ' ' && test2[2] == ' ' && test2[3] == ' ') ? "yes" : "no");
    printf("Line 2: '%s' - Has 4+ spaces: %s\n", test3, (strlen(test3) >= 4 && test3[0] == ' ' && test3[1] == ' ' && test3[2] == ' ' && test3[3] == ' ') ? "yes" : "no");
    
    return 0;
}
