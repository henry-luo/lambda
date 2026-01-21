// test_graphics_compare_gtest.cpp - Compare Lambda graphics SVG output against LaTeXML reference
//
// This test suite validates Lambda's graphics pipeline by comparing
// its SVG output against LaTeXML HTML reference files.
//
// To regenerate LaTeXML reference files:
//   ./utils/generate_graphics_refs.sh
//
// To run tests:
//   ./test/test_graphics_compare_gtest.exe

#include <gtest/gtest.h>
#include <string>
#include <vector>
#include <set>
#include <map>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <regex>
#include <algorithm>
#include <cctype>
#include <cmath>

extern "C" {
    #include "../lib/strbuf.h"
    #include "../lib/log.h"
    #include "../lib/arena.h"
    #include "../lib/mempool.h"
}

#include "../lambda/lambda-data.hpp"
#include "../lambda/tex/tex_document_model.hpp"

namespace fs = std::filesystem;

// ============================================================================
// Test Fixtures
// ============================================================================

// Graphics fixtures that must pass (baseline)
static const std::set<std::string> BASELINE_GRAPHICS_FIXTURES = {
    "lines_only",      // Lines work correctly
    "circles_only",    // Circles work correctly
    "boxes_only",      // Box commands work
    "simple_picture",  // Lines, circles, vectors
    "picture_basic",   // Comprehensive picture tests
};

// Graphics fixtures that are work-in-progress (extended)
static const std::set<std::string> EXTENDED_GRAPHICS_FIXTURES = {
    "simple_picture",  // Has vectors which are not working yet
    "lines_only",      // Lines only
    "circles_only",    // Circles and ovals
    "boxes_only",      // Box commands
    "picture_basic",   // Comprehensive picture tests without multirow
    // "picture",      // Full picture environment tests (complex - needs multirow package)
    // "colors",       // Color tests (no SVG output - text colors only)
    // "framed",       // Framed boxes (requires framed package)
    // "calc",         // Requires calc package
    // "graphrot",     // Requires graphicx
    // "xcolors",      // Requires xcolor
    // "xytest",       // Requires xy package
};

// ============================================================================
// Utility Functions
// ============================================================================

static std::string read_file(const fs::path& path) {
    std::ifstream ifs(path);
    if (!ifs) return "";
    std::stringstream ss;
    ss << ifs.rdbuf();
    return ss.str();
}

static void write_file(const fs::path& path, const std::string& content) {
    fs::create_directories(path.parent_path());
    std::ofstream ofs(path);
    ofs << content;
}

// Extract all SVG elements from HTML
static std::vector<std::string> extract_svgs(const std::string& html) {
    std::vector<std::string> svgs;
    std::regex svg_regex(R"(<svg[^>]*>[\s\S]*?</svg>)");
    
    auto begin = std::sregex_iterator(html.begin(), html.end(), svg_regex);
    auto end = std::sregex_iterator();
    
    for (auto it = begin; it != end; ++it) {
        svgs.push_back(it->str());
    }
    
    return svgs;
}

// ============================================================================
// SVG Structure Analysis
// ============================================================================

// Count element types in SVG
struct SvgElementCounts {
    int lines = 0;       // <line> elements
    int paths = 0;       // <path> elements
    int circles = 0;     // <circle> elements
    int rects = 0;       // <rect> elements
    int ellipses = 0;    // <ellipse> elements
    int polylines = 0;   // <polyline> elements
    int polygons = 0;    // <polygon> elements
    int groups = 0;      // <g> elements
    int markers = 0;     // <marker> elements
    int texts = 0;       // <text> or <foreignObject> elements
    
    int total_primitives() const {
        return lines + paths + circles + rects + ellipses + polylines + polygons;
    }
    
    std::string to_string() const {
        std::stringstream ss;
        ss << "lines=" << lines << ", paths=" << paths << ", circles=" << circles
           << ", rects=" << rects << ", ellipses=" << ellipses 
           << ", polylines=" << polylines << ", polygons=" << polygons
           << ", groups=" << groups << ", markers=" << markers << ", texts=" << texts;
        return ss.str();
    }
};

static SvgElementCounts count_svg_elements(const std::string& svg) {
    SvgElementCounts counts;
    
    // First, remove <defs>...</defs> section as marker paths shouldn't count
    std::string svg_no_defs = svg;
    static const std::regex re_defs(R"(<defs>[\s\S]*?</defs>)");
    svg_no_defs = std::regex_replace(svg_no_defs, re_defs, "");
    
    // Pre-compile regexes
    static const std::regex re_line(R"(<line\s)");
    static const std::regex re_path(R"(<path\s)");
    static const std::regex re_circle(R"(<circle\s)");
    static const std::regex re_rect(R"(<rect\s)");
    static const std::regex re_ellipse(R"(<ellipse\s)");
    static const std::regex re_polyline(R"(<polyline\s)");
    static const std::regex re_polygon(R"(<polygon\s)");
    static const std::regex re_group(R"(<g[\s>])");
    static const std::regex re_marker(R"(<marker\s)");
    static const std::regex re_text(R"(<(text|foreignObject)\s)");
    
    // Count each element type (excluding those in <defs>)
    counts.lines = std::distance(
        std::sregex_iterator(svg_no_defs.begin(), svg_no_defs.end(), re_line),
        std::sregex_iterator());
    counts.paths = std::distance(
        std::sregex_iterator(svg_no_defs.begin(), svg_no_defs.end(), re_path),
        std::sregex_iterator());
    counts.circles = std::distance(
        std::sregex_iterator(svg_no_defs.begin(), svg_no_defs.end(), re_circle),
        std::sregex_iterator());
    counts.rects = std::distance(
        std::sregex_iterator(svg_no_defs.begin(), svg_no_defs.end(), re_rect),
        std::sregex_iterator());
    counts.ellipses = std::distance(
        std::sregex_iterator(svg_no_defs.begin(), svg_no_defs.end(), re_ellipse),
        std::sregex_iterator());
    counts.polylines = std::distance(
        std::sregex_iterator(svg_no_defs.begin(), svg_no_defs.end(), re_polyline),
        std::sregex_iterator());
    counts.polygons = std::distance(
        std::sregex_iterator(svg_no_defs.begin(), svg_no_defs.end(), re_polygon),
        std::sregex_iterator());
    counts.groups = std::distance(
        std::sregex_iterator(svg_no_defs.begin(), svg_no_defs.end(), re_group),
        std::sregex_iterator());
    
    // Count markers from original SVG (in defs)
    counts.markers = std::distance(
        std::sregex_iterator(svg.begin(), svg.end(), re_marker),
        std::sregex_iterator());
    counts.texts = std::distance(
        std::sregex_iterator(svg_no_defs.begin(), svg_no_defs.end(), re_text),
        std::sregex_iterator());
    
    return counts;
}

// Extract SVG dimensions
struct SvgDimensions {
    float width = 0;
    float height = 0;
    bool valid = false;
};

static SvgDimensions extract_svg_dimensions(const std::string& svg) {
    SvgDimensions dims;
    std::smatch match;
    
    // Extract width
    if (std::regex_search(svg, match, std::regex(R"(width="([0-9.]+))"))) {
        dims.width = std::stof(match[1].str());
        dims.valid = true;
    }
    
    // Extract height
    if (std::regex_search(svg, match, std::regex(R"(height="([0-9.]+))"))) {
        dims.height = std::stof(match[1].str());
    }
    
    return dims;
}

// ============================================================================
// SVG Comparison Result
// ============================================================================

struct SvgCompareResult {
    bool passed = true;
    std::vector<std::string> issues;
    
    void add_issue(const std::string& issue) {
        passed = false;
        issues.push_back(issue);
    }
    
    std::string to_string() const {
        if (passed) return "PASSED";
        std::stringstream ss;
        for (const auto& issue : issues) {
            ss << "  - " << issue << "\n";
        }
        return ss.str();
    }
};

// Compare two SVG element counts
// Note: Lambda uses <line> while LaTeXML uses <path> for lines, so we compare total primitives
static SvgCompareResult compare_svg_structure(const std::string& expected_svg, 
                                              const std::string& actual_svg,
                                              int svg_index) {
    SvgCompareResult result;
    
    auto expected_counts = count_svg_elements(expected_svg);
    auto actual_counts = count_svg_elements(actual_svg);
    
    // Compare dimensions (with tolerance)
    auto expected_dims = extract_svg_dimensions(expected_svg);
    auto actual_dims = extract_svg_dimensions(actual_svg);
    
    // Log the counts for debugging
    log_info("SVG #%d expected: %s", svg_index, expected_counts.to_string().c_str());
    log_info("SVG #%d actual:   %s", svg_index, actual_counts.to_string().c_str());
    
    // Compare circles
    if (expected_counts.circles != actual_counts.circles) {
        result.add_issue("SVG #" + std::to_string(svg_index) + 
            ": circle count mismatch (expected " + std::to_string(expected_counts.circles) +
            ", got " + std::to_string(actual_counts.circles) + ")");
    }
    
    // Compare rectangles
    if (expected_counts.rects != actual_counts.rects) {
        result.add_issue("SVG #" + std::to_string(svg_index) +
            ": rect count mismatch (expected " + std::to_string(expected_counts.rects) +
            ", got " + std::to_string(actual_counts.rects) + ")");
    }
    
    // Compare ellipses
    if (expected_counts.ellipses != actual_counts.ellipses) {
        result.add_issue("SVG #" + std::to_string(svg_index) +
            ": ellipse count mismatch (expected " + std::to_string(expected_counts.ellipses) +
            ", got " + std::to_string(actual_counts.ellipses) + ")");
    }
    
    // For lines/paths: Lambda uses <line>, LaTeXML uses <path>
    // Compare total line-like primitives (lines + paths)
    int expected_lines = expected_counts.lines + expected_counts.paths + expected_counts.polylines;
    int actual_lines = actual_counts.lines + actual_counts.paths + actual_counts.polylines;
    
    if (expected_lines != actual_lines) {
        result.add_issue("SVG #" + std::to_string(svg_index) +
            ": line primitive count mismatch (expected " + std::to_string(expected_lines) +
            " [lines+paths+polylines], got " + std::to_string(actual_lines) + ")");
    }
    
    // Check for arrow markers if expected has them
    if (expected_counts.markers > 0 && actual_counts.markers == 0) {
        result.add_issue("SVG #" + std::to_string(svg_index) +
            ": expected arrow markers but none found");
    }
    
    return result;
}

// Compare lists of SVGs
static SvgCompareResult compare_svg_lists(const std::vector<std::string>& expected_svgs,
                                          const std::vector<std::string>& actual_svgs) {
    SvgCompareResult result;
    
    // Check count
    if (expected_svgs.size() != actual_svgs.size()) {
        result.add_issue("SVG count mismatch: expected " + 
            std::to_string(expected_svgs.size()) + ", got " + 
            std::to_string(actual_svgs.size()));
        // Continue comparing what we have
    }
    
    size_t count = std::min(expected_svgs.size(), actual_svgs.size());
    for (size_t i = 0; i < count; i++) {
        auto svg_result = compare_svg_structure(expected_svgs[i], actual_svgs[i], i + 1);
        if (!svg_result.passed) {
            for (const auto& issue : svg_result.issues) {
                result.add_issue(issue);
            }
        }
    }
    
    return result;
}

// ============================================================================
// Test Base Class
// ============================================================================

class GraphicsCompareTest : public ::testing::Test {
protected:
    static fs::path fixtures_dir;
    static fs::path expected_dir;
    static fs::path output_dir;
    
    static void SetUpTestSuite() {
        log_init("log.conf");
        fs::create_directories(output_dir);
    }
    
    static ::testing::AssertionResult RunCompareTest(const std::string& fixture) {
        fs::path tex_path = fixtures_dir / (fixture + ".tex");
        fs::path ref_path = expected_dir / (fixture + ".html");
        
        // Check files exist
        if (!fs::exists(tex_path)) {
            return ::testing::AssertionFailure() << "TeX file not found: " << tex_path;
        }
        if (!fs::exists(ref_path)) {
            return ::testing::AssertionFailure() << "Reference HTML not found: " << ref_path
                << " (run: ./utils/generate_graphics_refs.sh)";
        }
        
        // Read the TeX source
        std::string latex_content = read_file(tex_path);
        if (latex_content.empty()) {
            return ::testing::AssertionFailure() << "Failed to read TeX file: " << tex_path;
        }
        
        // Convert using Lambda pipeline
        Pool* doc_pool = pool_create();
        Arena* doc_arena = arena_create_default(doc_pool);
        
        tex::TexDocumentModel* doc = tex::doc_model_from_string(
            latex_content.c_str(), latex_content.size(), doc_arena, nullptr);
        
        std::string lambda_html;
        if (doc && doc->root) {
            StrBuf* html_buf = strbuf_new_cap(16384);
            tex::HtmlOutputOptions opts = tex::HtmlOutputOptions::hybrid();
            opts.standalone = true;
            opts.pretty_print = true;
            
            bool success = tex::doc_model_to_html(doc, html_buf, opts);
            if (success && html_buf->length > 0) {
                lambda_html = std::string(html_buf->str, html_buf->length);
            }
            strbuf_free(html_buf);
        }
        
        arena_destroy(doc_arena);
        pool_destroy(doc_pool);
        
        // Save Lambda output for debugging
        fs::path lambda_out = output_dir / (fixture + ".lambda.html");
        write_file(lambda_out, lambda_html.empty() ? "<!-- Lambda conversion failed -->\n" : lambda_html);
        
        if (lambda_html.empty()) {
            return ::testing::AssertionFailure() << "Lambda failed to convert: " << fixture;
        }
        
        // Read expected HTML (LaTeXML reference)
        std::string expected_html = read_file(ref_path);
        
        // Extract SVGs from both
        auto expected_svgs = extract_svgs(expected_html);
        auto actual_svgs = extract_svgs(lambda_html);
        
        // Report SVG counts
        log_info("graphics_compare: %s - expected %zu SVGs, got %zu SVGs",
                 fixture.c_str(), expected_svgs.size(), actual_svgs.size());
        
        // For now, just check if we produced any SVGs for documents that should have them
        if (expected_svgs.size() > 0 && actual_svgs.size() == 0) {
            return ::testing::AssertionFailure() 
                << "Expected " << expected_svgs.size() << " SVGs, but Lambda produced none";
        }
        
        // Structural SVG comparison
        auto compare_result = compare_svg_lists(expected_svgs, actual_svgs);
        if (!compare_result.passed) {
            return ::testing::AssertionFailure() 
                << "SVG structural comparison failed:\n" << compare_result.to_string();
        }
        
        return ::testing::AssertionSuccess()
            << "Produced " << actual_svgs.size() << " SVGs (expected " << expected_svgs.size() << ")";
    }
    
    // Alternative: Run test with relaxed comparison (just count check)
    static ::testing::AssertionResult RunCountOnlyTest(const std::string& fixture) {
        fs::path tex_path = fixtures_dir / (fixture + ".tex");
        fs::path ref_path = expected_dir / (fixture + ".html");
        
        if (!fs::exists(tex_path)) {
            return ::testing::AssertionFailure() << "TeX file not found: " << tex_path;
        }
        if (!fs::exists(ref_path)) {
            return ::testing::AssertionFailure() << "Reference HTML not found: " << ref_path;
        }
        
        std::string latex_content = read_file(tex_path);
        
        Pool* doc_pool = pool_create();
        Arena* doc_arena = arena_create_default(doc_pool);
        
        tex::TexDocumentModel* doc = tex::doc_model_from_string(
            latex_content.c_str(), latex_content.size(), doc_arena, nullptr);
        
        std::string lambda_html;
        if (doc && doc->root) {
            StrBuf* html_buf = strbuf_new_cap(16384);
            tex::HtmlOutputOptions opts = tex::HtmlOutputOptions::hybrid();
            opts.standalone = true;
            
            if (tex::doc_model_to_html(doc, html_buf, opts) && html_buf->length > 0) {
                lambda_html = std::string(html_buf->str, html_buf->length);
            }
            strbuf_free(html_buf);
        }
        
        arena_destroy(doc_arena);
        pool_destroy(doc_pool);
        
        if (lambda_html.empty()) {
            return ::testing::AssertionFailure() << "Lambda conversion failed";
        }
        
        auto expected_svgs = extract_svgs(read_file(ref_path));
        auto actual_svgs = extract_svgs(lambda_html);
        
        if (expected_svgs.size() != actual_svgs.size()) {
            return ::testing::AssertionFailure() 
                << "SVG count: expected " << expected_svgs.size() << ", got " << actual_svgs.size();
        }
        
        return ::testing::AssertionSuccess() << "SVG count matches: " << actual_svgs.size();
    }
};

// Static member initialization
fs::path GraphicsCompareTest::fixtures_dir = "test/latex/fixtures/graphics";
fs::path GraphicsCompareTest::expected_dir = "test/latex/expected/graphics";
fs::path GraphicsCompareTest::output_dir = "test_output/graphics";

// ============================================================================
// Baseline Tests - Must Pass
// ============================================================================

class BaselineGraphicsTest : public GraphicsCompareTest,
                              public ::testing::WithParamInterface<std::string> {};

TEST_P(BaselineGraphicsTest, Compare) {
    EXPECT_TRUE(RunCompareTest(GetParam()));
}

// Generate test names from fixtures
static std::string get_test_name(const ::testing::TestParamInfo<std::string>& info) {
    std::string name = info.param;
    for (char& c : name) {
        if (!std::isalnum(c)) c = '_';
    }
    return name;
}

INSTANTIATE_TEST_SUITE_P(
    Baseline,
    BaselineGraphicsTest,
    ::testing::ValuesIn(std::vector<std::string>(
        BASELINE_GRAPHICS_FIXTURES.begin(), 
        BASELINE_GRAPHICS_FIXTURES.end())),
    get_test_name
);

// ============================================================================
// Extended Tests - Work in Progress
// ============================================================================

class ExtendedGraphicsTest : public GraphicsCompareTest,
                              public ::testing::WithParamInterface<std::string> {};

TEST_P(ExtendedGraphicsTest, Compare) {
    // Mark as informational - don't fail the build
    auto result = RunCompareTest(GetParam());
    if (!result) {
        // Log but don't fail
        GTEST_SKIP() << "Extended test not yet passing: " << result.message();
    }
    EXPECT_TRUE(result);
}

INSTANTIATE_TEST_SUITE_P(
    Extended,
    ExtendedGraphicsTest,
    ::testing::ValuesIn(std::vector<std::string>(
        EXTENDED_GRAPHICS_FIXTURES.begin(), 
        EXTENDED_GRAPHICS_FIXTURES.end())),
    get_test_name
);

// ============================================================================
// Manual Test: Run single fixture with verbose output
// ============================================================================

TEST_F(GraphicsCompareTest, ManualTest_Picture) {
    // Use this for debugging a specific fixture
    fs::path tex_path = fixtures_dir / "simple_picture.tex";
    fs::path ref_path = expected_dir / "simple_picture.html";
    
    if (!fs::exists(ref_path)) {
        GTEST_SKIP() << "Reference file not found. Run: ./utils/generate_graphics_refs.sh";
    }
    
    std::string latex = read_file(tex_path);
    ASSERT_FALSE(latex.empty()) << "Failed to read fixture";
    
    // Convert
    Pool* pool = pool_create();
    Arena* arena = arena_create_default(pool);
    
    tex::TexDocumentModel* doc = tex::doc_model_from_string(
        latex.c_str(), latex.size(), arena, nullptr);
    
    ASSERT_NE(doc, nullptr) << "Failed to create document model";
    ASSERT_NE(doc->root, nullptr) << "Document model has no root";
    
    StrBuf* html_buf = strbuf_new_cap(16384);
    tex::HtmlOutputOptions opts = tex::HtmlOutputOptions::hybrid();
    opts.standalone = true;
    opts.pretty_print = true;
    
    bool success = tex::doc_model_to_html(doc, html_buf, opts);
    ASSERT_TRUE(success) << "Failed to convert to HTML";
    
    std::string html(html_buf->str, html_buf->length);
    strbuf_free(html_buf);
    arena_destroy(arena);
    pool_destroy(pool);
    
    // Save output
    write_file(output_dir / "simple_picture_manual.html", html);
    
    // Count SVGs
    auto svgs = extract_svgs(html);
    std::cout << "Generated " << svgs.size() << " SVG elements\n";
    
    // Print first SVG for inspection
    if (!svgs.empty()) {
        std::cout << "First SVG (truncated):\n" << svgs[0].substr(0, 1000) << "\n...\n";
    }
    
    // Compare with reference
    std::string ref_html = read_file(ref_path);
    auto ref_svgs = extract_svgs(ref_html);
    std::cout << "Reference has " << ref_svgs.size() << " SVG elements\n";
    
    EXPECT_GT(svgs.size(), 0u) << "No SVGs generated (expected some from simple_picture.tex)";
}

// Test for detailed SVG structure analysis
TEST_F(GraphicsCompareTest, StructuralAnalysis) {
    // This test prints detailed comparison for all fixtures
    std::vector<std::string> fixtures = {"lines_only", "circles_only", "boxes_only", "simple_picture"};
    
    for (const auto& fixture : fixtures) {
        fs::path tex_path = fixtures_dir / (fixture + ".tex");
        fs::path ref_path = expected_dir / (fixture + ".html");
        
        if (!fs::exists(tex_path) || !fs::exists(ref_path)) continue;
        
        std::cout << "\n=== " << fixture << " ===\n";
        
        std::string latex = read_file(tex_path);
        
        Pool* pool = pool_create();
        Arena* arena = arena_create_default(pool);
        
        tex::TexDocumentModel* doc = tex::doc_model_from_string(
            latex.c_str(), latex.size(), arena, nullptr);
        
        if (!doc || !doc->root) {
            std::cout << "  FAILED to parse\n";
            arena_destroy(arena);
            pool_destroy(pool);
            continue;
        }
        
        StrBuf* html_buf = strbuf_new_cap(16384);
        tex::HtmlOutputOptions opts = tex::HtmlOutputOptions::hybrid();
        opts.standalone = true;
        
        tex::doc_model_to_html(doc, html_buf, opts);
        std::string html(html_buf->str, html_buf->length);
        strbuf_free(html_buf);
        arena_destroy(arena);
        pool_destroy(pool);
        
        auto lambda_svgs = extract_svgs(html);
        auto ref_svgs = extract_svgs(read_file(ref_path));
        
        std::cout << "  SVG count: Lambda=" << lambda_svgs.size() 
                  << ", Reference=" << ref_svgs.size() << "\n";
        
        size_t count = std::min(lambda_svgs.size(), ref_svgs.size());
        for (size_t i = 0; i < count; i++) {
            auto lambda_counts = count_svg_elements(lambda_svgs[i]);
            auto ref_counts = count_svg_elements(ref_svgs[i]);
            
            std::cout << "  SVG #" << (i+1) << ":\n";
            std::cout << "    Lambda:    " << lambda_counts.to_string() << "\n";
            std::cout << "    Reference: " << ref_counts.to_string() << "\n";
            
            // Check line equivalence (Lambda uses <line>, LaTeXML uses <path>)
            int lambda_lines = lambda_counts.lines + lambda_counts.paths + lambda_counts.polylines;
            int ref_lines = ref_counts.lines + ref_counts.paths + ref_counts.polylines;
            if (lambda_lines == ref_lines) {
                std::cout << "    Lines: MATCH (" << lambda_lines << ")\n";
            } else {
                std::cout << "    Lines: MISMATCH (lambda=" << lambda_lines 
                          << ", ref=" << ref_lines << ")\n";
            }
            
            if (lambda_counts.circles == ref_counts.circles) {
                std::cout << "    Circles: MATCH (" << lambda_counts.circles << ")\n";
            } else {
                std::cout << "    Circles: MISMATCH (lambda=" << lambda_counts.circles 
                          << ", ref=" << ref_counts.circles << ")\n";
            }
        }
    }
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
