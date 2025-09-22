#include <gtest/gtest.h>
#include "../lambda/input/css_parser.h"
#include "../lib/mem-pool/include/mem_pool.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Test fixture class for CSS frameworks tests
class CssFrameworksTest : public ::testing::Test {
protected:
    void SetUp() override {
        MemPoolError err = pool_variable_init(&pool, 4 * 1024 * 1024, 10);  // 4MB pool for large CSS files
        ASSERT_EQ(err, MEM_POOL_ERR_OK) << "Failed to create memory pool";
        parser = css_parser_create(pool);
        ASSERT_NE(parser, nullptr) << "Failed to create CSS parser";
    }
    
    void TearDown() override {
        if (parser) {
            css_parser_destroy(parser);
        }
        if (pool) {
            pool_variable_destroy(pool);
        }
    }
    
    // Helper function to read file contents
    char* read_css_file(const char* filename, size_t* file_size) {
        FILE* file = fopen(filename, "r");
        if (!file) {
            return NULL;
        }
        
        fseek(file, 0, SEEK_END);
        *file_size = ftell(file);
        fseek(file, 0, SEEK_SET);
        
        char* content = (char*)malloc(*file_size + 1);
        if (!content) {
            fclose(file);
            return NULL;
        }
        
        fread(content, 1, *file_size, file);
        content[*file_size] = '\0';
        fclose(file);
        
        return content;
    }
    
    // Helper function to format file size for display
    const char* format_size(size_t bytes) {
        static char buffer[64];
        
        if (bytes >= 1024 * 1024) {
            snprintf(buffer, sizeof(buffer), "%.2f MB", bytes / (1024.0 * 1024.0));
        } else if (bytes >= 1024) {
            snprintf(buffer, sizeof(buffer), "%.2f KB", bytes / 1024.0);
        } else {
            snprintf(buffer, sizeof(buffer), "%zu bytes", bytes);
        }
        
        return buffer;
    }
    
    VariableMemPool* pool = nullptr;
    css_parser_t* parser = nullptr;
};

// Test Bootstrap CSS parsing
TEST_F(CssFrameworksTest, ParseBootstrap) {
    size_t file_size;
    char* css_content = read_css_file("test/input/bootstrap.css", &file_size);
    ASSERT_NE(css_content, nullptr) << "Failed to read Bootstrap CSS file";
    
    printf("📄 Bootstrap CSS size: %s\n", format_size(file_size));
    
    clock_t start_time = clock();
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css_content);
    clock_t end_time = clock();
    
    double parse_time = ((double)(end_time - start_time)) / CLOCKS_PER_SEC;
    printf("⏱️  Bootstrap parse time: %.3f seconds\n", parse_time);
    
    ASSERT_NE(stylesheet, nullptr) << "Bootstrap CSS parsing should not return NULL";
    EXPECT_GT(stylesheet->rule_count, 0) << "Bootstrap CSS should have rules";
    
    if (stylesheet->rule_count > 0) {
        printf("📈 Bootstrap rules found: %d\n", stylesheet->rule_count);
        double speed_mb_per_sec = (file_size / (1024.0 * 1024.0)) / parse_time;
        printf("🚀 Bootstrap parsing speed: %.2f MB/s\n", speed_mb_per_sec);
    }
    
    free(css_content);
}

// Test Bulma CSS parsing
TEST_F(CssFrameworksTest, ParseBulma) {
    size_t file_size;
    char* css_content = read_css_file("test/input/bulma.css", &file_size);
    ASSERT_NE(css_content, nullptr) << "Failed to read Bulma CSS file";
    
    printf("📄 Bulma CSS size: %s\n", format_size(file_size));
    
    clock_t start_time = clock();
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css_content);
    clock_t end_time = clock();
    
    double parse_time = ((double)(end_time - start_time)) / CLOCKS_PER_SEC;
    printf("⏱️  Bulma parse time: %.3f seconds\n", parse_time);
    
    ASSERT_NE(stylesheet, nullptr) << "Bulma CSS parsing should not return NULL";
    EXPECT_GT(stylesheet->rule_count, 0) << "Bulma CSS should have rules";
    
    if (stylesheet->rule_count > 0) {
        printf("📈 Bulma rules found: %d\n", stylesheet->rule_count);
        double speed_mb_per_sec = (file_size / (1024.0 * 1024.0)) / parse_time;
        printf("🚀 Bulma parsing speed: %.2f MB/s\n", speed_mb_per_sec);
    }
    
    free(css_content);
}

// Test Foundation CSS parsing
TEST_F(CssFrameworksTest, ParseFoundation) {
    size_t file_size;
    char* css_content = read_css_file("test/input/foundation.css", &file_size);
    ASSERT_NE(css_content, nullptr) << "Failed to read Foundation CSS file";
    
    printf("📄 Foundation CSS size: %s\n", format_size(file_size));
    
    clock_t start_time = clock();
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css_content);
    clock_t end_time = clock();
    
    double parse_time = ((double)(end_time - start_time)) / CLOCKS_PER_SEC;
    printf("⏱️  Foundation parse time: %.3f seconds\n", parse_time);
    
    ASSERT_NE(stylesheet, nullptr) << "Foundation CSS parsing should not return NULL";
    EXPECT_GT(stylesheet->rule_count, 0) << "Foundation CSS should have rules";
    
    if (stylesheet->rule_count > 0) {
        printf("📈 Foundation rules found: %d\n", stylesheet->rule_count);
        double speed_mb_per_sec = (file_size / (1024.0 * 1024.0)) / parse_time;
        printf("🚀 Foundation parsing speed: %.2f MB/s\n", speed_mb_per_sec);
    }
    
    free(css_content);
}

// Test Normalize CSS parsing
TEST_F(CssFrameworksTest, ParseNormalize) {
    size_t file_size;
    char* css_content = read_css_file("test/input/normalize.css", &file_size);
    ASSERT_NE(css_content, nullptr) << "Failed to read Normalize CSS file";
    
    printf("📄 Normalize CSS size: %s\n", format_size(file_size));
    
    clock_t start_time = clock();
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css_content);
    clock_t end_time = clock();
    
    double parse_time = ((double)(end_time - start_time)) / CLOCKS_PER_SEC;
    printf("⏱️  Normalize parse time: %.3f seconds\n", parse_time);
    
    ASSERT_NE(stylesheet, nullptr) << "Normalize CSS parsing should not return NULL";
    EXPECT_GT(stylesheet->rule_count, 0) << "Normalize CSS should have rules";
    
    if (stylesheet->rule_count > 0) {
        printf("📈 Normalize rules found: %d\n", stylesheet->rule_count);
        double speed_mb_per_sec = (file_size / (1024.0 * 1024.0)) / parse_time;
        printf("🚀 Normalize parsing speed: %.2f MB/s\n", speed_mb_per_sec);
    }
    
    free(css_content);
}

// Test Tailwind CSS parsing
TEST_F(CssFrameworksTest, ParseTailwind) {
    size_t file_size;
    char* css_content = read_css_file("test/input/tailwind.css", &file_size);
    ASSERT_NE(css_content, nullptr) << "Failed to read Tailwind CSS file";
    
    printf("📄 Tailwind CSS size: %s\n", format_size(file_size));
    
    clock_t start_time = clock();
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css_content);
    clock_t end_time = clock();
    
    double parse_time = ((double)(end_time - start_time)) / CLOCKS_PER_SEC;
    printf("⏱️  Tailwind parse time: %.3f seconds\n", parse_time);
    
    ASSERT_NE(stylesheet, nullptr) << "Tailwind CSS parsing should not return NULL";
    EXPECT_GT(stylesheet->rule_count, 0) << "Tailwind CSS should have rules";
    
    if (stylesheet->rule_count > 0) {
        printf("📈 Tailwind rules found: %d\n", stylesheet->rule_count);
        double speed_mb_per_sec = (file_size / (1024.0 * 1024.0)) / parse_time;
        printf("🚀 Tailwind parsing speed: %.2f MB/s\n", speed_mb_per_sec);
    }
    
    free(css_content);
}

// Test all frameworks performance
TEST_F(CssFrameworksTest, ParseAllFrameworksPerformance) {
    const char* frameworks[] = {
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
    const int num_frameworks = sizeof(frameworks) / sizeof(frameworks[0]);
    
    printf("\n🏁 Performance test for all CSS frameworks:\n");
    
    double total_parse_time = 0.0;
    size_t total_file_size = 0;
    int successful_parses = 0;
    
    for (int i = 0; i < num_frameworks; i++) {
        size_t file_size;
        char* css_content = read_css_file(frameworks[i], &file_size);
        
        if (!css_content) {
            printf("⚠️  Skipping %s (file not found)\n", framework_names[i]);
            continue;
        }
        
        clock_t start_time = clock();
        css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css_content);
        clock_t end_time = clock();
        
        double parse_time = ((double)(end_time - start_time)) / CLOCKS_PER_SEC;
        
        if (stylesheet && stylesheet->rule_count > 0) {
            printf("✅ %s: %s, %d rules, %.3fs\n", 
                   framework_names[i], format_size(file_size), 
                   stylesheet->rule_count, parse_time);
            total_parse_time += parse_time;
            total_file_size += file_size;
            successful_parses++;
        } else {
            printf("❌ %s: Parse failed\n", framework_names[i]);
        }
        
        free(css_content);
    }
    
    EXPECT_GT(successful_parses, 0) << "At least one framework should parse successfully";
    
    if (successful_parses > 0) {
        double avg_speed = (total_file_size / (1024.0 * 1024.0)) / total_parse_time;
        printf("\n📊 Overall performance:\n");
        printf("   Total size: %s\n", format_size(total_file_size));
        printf("   Total time: %.3f seconds\n", total_parse_time);
        printf("   Average speed: %.2f MB/s\n", avg_speed);
        printf("   Successful parses: %d/%d\n", successful_parses, num_frameworks);
    }
}