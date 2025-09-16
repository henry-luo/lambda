#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

extern "C" {
#include "../lambda/input/css_parser.h"
#include "../lib/mem-pool/include/mem_pool.h"
}

// Global variables for setup/teardown
static VariableMemPool* pool = nullptr;
static css_parser_t* parser = nullptr;

void setup_css_frameworks() {
    if (!pool) {
        MemPoolError err = pool_variable_init(&pool, 4 * 1024 * 1024, 10);  // 4MB pool for large CSS files
        REQUIRE(err == MEM_POOL_ERR_OK);
        parser = css_parser_create(pool);
        REQUIRE(parser != nullptr);
    }
}

void teardown_css_frameworks() {
    if (parser) {
        css_parser_destroy(parser);
        parser = nullptr;
    }
    if (pool) {
        pool_variable_destroy(pool);
        pool = nullptr;
    }
}

// Helper function to read file contents
static char* read_css_file(const char* filename, size_t* file_size) {
    FILE* file = fopen(filename, "r");
    if (!file) {
        return nullptr;
    }
    
    fseek(file, 0, SEEK_END);
    *file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    char* content = (char*)malloc(*file_size + 1);
    if (!content) {
        fclose(file);
        return nullptr;
    }
    
    fread(content, 1, *file_size, file);
    content[*file_size] = '\0';
    fclose(file);
    
    return content;
}

// Helper function to format file size for logging
static const char* format_size(size_t bytes) {
    static char buffer[32];
    if (bytes < 1024) {
        snprintf(buffer, sizeof(buffer), "%zu B", bytes);
    } else if (bytes < 1024 * 1024) {
        snprintf(buffer, sizeof(buffer), "%.1f KB", bytes / 1024.0);
    } else {
        snprintf(buffer, sizeof(buffer), "%.1f MB", bytes / (1024.0 * 1024.0));
    }
    return buffer;
}

TEST_CASE("CSS Frameworks - Bootstrap", "[css][frameworks][bootstrap]") {
    setup_css_frameworks();
    
    size_t file_size;
    char* css_content = read_css_file("test/input/bootstrap.css", &file_size);
    REQUIRE(css_content != nullptr);
    
    printf("📄 Bootstrap CSS size: %s\n", format_size(file_size));
    
    clock_t start_time = clock();
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css_content);
    clock_t end_time = clock();
    
    double parse_time = ((double)(end_time - start_time)) / CLOCKS_PER_SEC;
    printf("⏱️  Bootstrap parse time: %.3f seconds\n", parse_time);
    
    REQUIRE(stylesheet != nullptr);
    REQUIRE(stylesheet->rule_count > 0);
    
    if (stylesheet->rule_count > 0) {
        printf("📈 Bootstrap rules found: %d\n", stylesheet->rule_count);
        double speed_mb_per_sec = (file_size / (1024.0 * 1024.0)) / parse_time;
        printf("🚀 Bootstrap parsing speed: %.2f MB/s\n", speed_mb_per_sec);
    }
    
    free(css_content);
    teardown_css_frameworks();
}

TEST_CASE("CSS Frameworks - Bulma", "[css][frameworks][bulma]") {
    setup_css_frameworks();
    
    size_t file_size;
    char* css_content = read_css_file("test/input/bulma.css", &file_size);
    REQUIRE(css_content != nullptr);
    
    printf("📄 Bulma CSS size: %s\n", format_size(file_size));
    
    clock_t start_time = clock();
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css_content);
    clock_t end_time = clock();
    
    double parse_time = ((double)(end_time - start_time)) / CLOCKS_PER_SEC;
    printf("⏱️  Bulma parse time: %.3f seconds\n", parse_time);
    
    REQUIRE(stylesheet != nullptr);
    REQUIRE(stylesheet->rule_count > 0);
    
    if (stylesheet->rule_count > 0) {
        printf("📈 Bulma rules found: %d\n", stylesheet->rule_count);
        double speed_mb_per_sec = (file_size / (1024.0 * 1024.0)) / parse_time;
        printf("🚀 Bulma parsing speed: %.2f MB/s\n", speed_mb_per_sec);
    }
    
    free(css_content);
    teardown_css_frameworks();
}

TEST_CASE("CSS Frameworks - Foundation", "[css][frameworks][foundation]") {
    setup_css_frameworks();
    
    size_t file_size;
    char* css_content = read_css_file("test/input/foundation.css", &file_size);
    REQUIRE(css_content != nullptr);
    
    printf("📄 Foundation CSS size: %s\n", format_size(file_size));
    
    clock_t start_time = clock();
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css_content);
    clock_t end_time = clock();
    
    double parse_time = ((double)(end_time - start_time)) / CLOCKS_PER_SEC;
    printf("⏱️  Foundation parse time: %.3f seconds\n", parse_time);
    
    REQUIRE(stylesheet != nullptr);
    REQUIRE(stylesheet->rule_count > 0);
    
    if (stylesheet->rule_count > 0) {
        printf("📈 Foundation rules found: %d\n", stylesheet->rule_count);
        double speed_mb_per_sec = (file_size / (1024.0 * 1024.0)) / parse_time;
        printf("🚀 Foundation parsing speed: %.2f MB/s\n", speed_mb_per_sec);
    }
    
    free(css_content);
    teardown_css_frameworks();
}

TEST_CASE("CSS Frameworks - Normalize", "[css][frameworks][normalize]") {
    setup_css_frameworks();
    
    size_t file_size;
    char* css_content = read_css_file("test/input/normalize.css", &file_size);
    REQUIRE(css_content != nullptr);
    
    printf("📄 Normalize CSS size: %s\n", format_size(file_size));
    
    clock_t start_time = clock();
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css_content);
    clock_t end_time = clock();
    
    double parse_time = ((double)(end_time - start_time)) / CLOCKS_PER_SEC;
    printf("⏱️  Normalize parse time: %.3f seconds\n", parse_time);
    
    REQUIRE(stylesheet != nullptr);
    REQUIRE(stylesheet->rule_count > 0);
    
    if (stylesheet->rule_count > 0) {
        printf("📈 Normalize rules found: %d\n", stylesheet->rule_count);
        double speed_mb_per_sec = (file_size / (1024.0 * 1024.0)) / parse_time;
        printf("🚀 Normalize parsing speed: %.2f MB/s\n", speed_mb_per_sec);
    }
    
    free(css_content);
    teardown_css_frameworks();
}

TEST_CASE("CSS Frameworks - Tailwind", "[css][frameworks][tailwind]") {
    setup_css_frameworks();
    
    size_t file_size;
    char* css_content = read_css_file("test/input/tailwind.css", &file_size);
    REQUIRE(css_content != nullptr);
    
    printf("📄 Tailwind CSS size: %s\n", format_size(file_size));
    
    clock_t start_time = clock();
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css_content);
    clock_t end_time = clock();
    
    double parse_time = ((double)(end_time - start_time)) / CLOCKS_PER_SEC;
    printf("⏱️  Tailwind parse time: %.3f seconds\n", parse_time);
    
    REQUIRE(stylesheet != nullptr);
    REQUIRE(stylesheet->rule_count > 0);
    
    if (stylesheet->rule_count > 0) {
        printf("📈 Tailwind rules found: %d\n", stylesheet->rule_count);
        double speed_mb_per_sec = (file_size / (1024.0 * 1024.0)) / parse_time;
        printf("🚀 Tailwind parsing speed: %.2f MB/s\n", speed_mb_per_sec);
    }
    
    free(css_content);
    teardown_css_frameworks();
}

TEST_CASE("CSS Frameworks - Performance Test All", "[css][frameworks][performance]") {
    setup_css_frameworks();
    
    const char* css_files[] = {
        "test/input/bootstrap.css",
        "test/input/bulma.css", 
        "test/input/foundation.css",
        "test/input/normalize.css",
        "test/input/tailwind.css"
    };
    
    const char* framework_names[] = {
        "Bootstrap",
        "Bulma",
        "Foundation", 
        "Normalize",
        "Tailwind"
    };
    
    int total_files = sizeof(css_files) / sizeof(css_files[0]);
    size_t total_size = 0;
    double total_time = 0.0;
    int total_rules = 0;
    int successful_parses = 0;
    
    printf("\n🧪 CSS Framework Performance Test Summary\n");
    printf("═══════════════════════════════════════════════════════════\n");
    
    for (int i = 0; i < total_files; i++) {
        size_t file_size;
        char* css_content = read_css_file(css_files[i], &file_size);
        
        if (!css_content) {
            printf("⚠️  Skipping %s (file not found)\n", framework_names[i]);
            continue;
        }
        
        clock_t start_time = clock();
        css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css_content);
        clock_t end_time = clock();
        
        double parse_time = ((double)(end_time - start_time)) / CLOCKS_PER_SEC;
        
        total_size += file_size;
        total_time += parse_time;
        
        if (stylesheet && stylesheet->rule_count > 0) {
            total_rules += stylesheet->rule_count;
            successful_parses++;
            printf("✅ %s: %s, %d rules, %.3fs\n", 
                   framework_names[i], format_size(file_size), 
                   stylesheet->rule_count, parse_time);
        } else {
            printf("❌ %s: Parse failed\n", framework_names[i]);
        }
        
        free(css_content);
    }
    
    printf("═══════════════════════════════════════════════════════════\n");
    printf("📊 Total size processed: %s\n", format_size(total_size));
    printf("⏱️  Total parse time: %.3f seconds\n", total_time);
    printf("📈 Total rules parsed: %d\n", total_rules);
    printf("✅ Successful parses: %d/%d\n", successful_parses, total_files);
    
    if (total_time > 0) {
        double overall_speed = (total_size / (1024.0 * 1024.0)) / total_time;
        printf("🚀 Overall parsing speed: %.2f MB/s\n", overall_speed);
    }
    
    // Assert that we successfully parsed at least 80% of the frameworks
    REQUIRE(successful_parses >= (int)(total_files * 0.8));
    
    // Assert that we found a reasonable number of rules
    REQUIRE(total_rules > 100);
    
    teardown_css_frameworks();
}
