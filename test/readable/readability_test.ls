// Test suite for readability.ls implementation
// Uses test cases from the readability.js test suite
//
// Run: ./lambda.exe test/readable/readability_test.ls

import readability: .utils.readability;

// Test case directory
let TEST_PAGES_DIR = "./readability/test/test-pages/";

// List of selected test cases (start with a core subset)
let CORE_TEST_CASES = [
    "001",
    "002", 
    "mozilla-1",
    "nytimes-1",
    "medium-1",
    "wikipedia"
];

// ============================================
// Test Helper Functions
// ============================================

/// Load a test case and run readability
fn run_test(test_name) {
    let source_path = TEST_PAGES_DIR ++ test_name ++ "/source.html";
    let metadata_path = TEST_PAGES_DIR ++ test_name ++ "/expected-metadata.json";
    
    // Load source HTML
    let html_content = read(source_path);
    if (html_content is error) {
        return {
            test: test_name,
            passed: false,
            error: "Failed to load source: " ++ string(html_content)
        }
    };
    
    // Load expected metadata
    let expected_json = read(metadata_path);
    if (expected_json is error) {
        return {
            test: test_name,
            passed: false,
            error: "Failed to load expected metadata: " ++ string(expected_json)
        }
    };
    
    let expected = input(expected_json, 'json);
    if (expected is error) {
        return {
            test: test_name,
            passed: false,
            error: "Failed to parse expected metadata"
        }
    };
    
    // Run readability parser
    let result = readability.parse(html_content);
    
    // Compare results
    let title_match = compare_field(result.title, expected.title, "title");
    let byline_match = compare_field(result.byline, expected.byline, "byline");
    let lang_match = compare_field(result.lang, expected.lang, "lang");
    let siteName_match = compare_field(result.siteName, expected.siteName, "siteName");
    
    let all_passed = title_match.passed and byline_match.passed and 
                     lang_match.passed and siteName_match.passed;
    
    let failures = [];
    let failures = if (not title_match.passed) { failures ++ [title_match.message] } else { failures };
    let failures = if (not byline_match.passed) { failures ++ [byline_match.message] } else { failures };
    let failures = if (not lang_match.passed) { failures ++ [lang_match.message] } else { failures };
    let failures = if (not siteName_match.passed) { failures ++ [siteName_match.message] } else { failures };
    
    {
        test: test_name,
        passed: all_passed,
        failures: failures,
        result: {
            title: result.title,
            byline: result.byline,
            lang: result.lang,
            siteName: result.siteName,
            excerpt: result.excerpt,
            textContent_length: result.length
        },
        expected: {
            title: expected.title,
            byline: expected.byline,
            lang: expected.lang,
            siteName: expected.siteName,
            excerpt: expected.excerpt
        }
    }
}

/// Compare a single field
fn compare_field(actual, expected, field_name) {
    if (expected == null) {
        // If expected is null, we don't strictly enforce the actual value
        {passed: true, message: ""}
    }
    else if (actual == expected) {
        {passed: true, message: ""}
    }
    else {
        // For titles, check if actual contains expected (handles separator stripping)
        let is_close = if (field_name == "title" and actual != null and expected != null) {
            contains(actual, expected) or contains(expected, actual)
        } else { false };
        
        if (is_close) {
            {passed: true, message: field_name ++ " is close enough"}
        }
        else {
            {
                passed: false, 
                message: field_name ++ " mismatch: expected '" ++ string(expected) ++ 
                         "', got '" ++ string(actual) ++ "'"
            }
        }
    }
}

// ============================================
// Run Tests
// ============================================

/// Run all core test cases
fn run_all_tests() {
    let results = for (test_name in CORE_TEST_CASES) run_test(test_name);
    
    let passed_count = len(for (r in results) if (r.passed) r);
    let total_count = len(CORE_TEST_CASES);
    
    {
        summary: "Passed " ++ string(passed_count) ++ "/" ++ string(total_count) ++ " tests",
        results: results
    }
}

// ============================================
// Main Output
// ============================================

"=== Readability Test Suite ===" ++ "\n" ++
"Running " ++ string(len(CORE_TEST_CASES)) ++ " core test cases..." ++ "\n\n" ++
format(run_all_tests(), 'json)
