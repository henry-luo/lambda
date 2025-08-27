#include "lib/log.h"

int main() {
    // Initialize logging with the enhanced log.conf
    log_parse_config_file("log.conf");
    log_init("");
    
    printf("=== Enhanced Lambda Script Logger Demo ===\n\n");
    
    // Test the improved default format
    printf("1. Default format - time only, space before level, hidden default category:\n");
    log_debug("transpile box item: 10");
    log_info("Processing JSON input file");
    log_warn("Memory usage is high");
    log_error("Failed to parse expression");
    
    // Test custom categories
    printf("\n2. Custom categories - shown when not 'default':\n");
    log_category_t *parser = log_get_category("parser");
    log_category_t *memory = log_get_category("memory");
    
    clog_debug(parser, "Started parsing lambda expression");
    clog_info(memory, "Allocated new memory pool: 1024KB");
    clog_warn(parser, "Unexpected token in input");
    clog_error(memory, "Memory pool exhausted");
    
    printf("\n3. Log levels and routing:\n");
    printf("   - DEBUG/INFO -> stdout\n");
    printf("   - WARN/ERROR -> stderr\n");
    printf("   - All levels also logged to 'log.txt'\n");
    
    log_fini();
    
    printf("\n=== Log file created: log.txt ===\n");
    printf("Check the log file to see all messages were written there too.\n");
    
    return 0;
}
