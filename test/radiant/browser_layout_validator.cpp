#include "browser_layout_validator.hpp"
#include "../radiant/layout.hpp"
#include "../radiant/view.hpp"
#include <json/json.h>
#include <fstream>
#include <sstream>
#include <regex>
#include <algorithm>

namespace radiant {
namespace testing {

std::unique_ptr<LayoutTestDescriptor> BrowserLayoutValidator::loadTestDescriptor(const std::string& json_file) {
    std::ifstream file(json_file);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open test descriptor file: " + json_file);
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    return parseTestDescriptor(buffer.str());
}

std::unique_ptr<LayoutTestDescriptor> BrowserLayoutValidator::parseTestDescriptor(const std::string& json_content) {
    Json::Value root;
    Json::Reader reader;
    
    if (!reader.parse(json_content, root)) {
        throw std::runtime_error("Failed to parse JSON: " + reader.getFormattedErrorMessages());
    }
    
    auto descriptor = std::make_unique<LayoutTestDescriptor>();
    
    descriptor->test_id = root["test_id"].asString();
    descriptor->category = root["category"].asString();
    descriptor->spec_reference = root["spec_reference"].asString();
    descriptor->description = root["description"].asString();
    descriptor->html = root["html"].asString();
    descriptor->css = root["css"].asString();
    descriptor->browser_engine = root["browser_engine"].asString();
    descriptor->browser_version = root["browser_version"].asString();
    descriptor->extraction_date = root["extraction_date"].asString();
    descriptor->tolerance_px = root["tolerance_px"].asDouble();
    
    // Parse properties to test
    for (const auto& prop : root["properties_to_test"]) {
        descriptor->properties_to_test.push_back(prop.asString());
    }
    
    // Parse expected layout
    const Json::Value& layout = root["expected_layout"];
    for (const auto& selector : layout.getMemberNames()) {
        ExpectedElement element;
        element.selector = selector;
        
        const Json::Value& elem = layout[selector];
        element.rect.x = elem["x"].asInt();
        element.rect.y = elem["y"].asInt();
        element.rect.width = elem["width"].asInt();
        element.rect.height = elem["height"].asInt();
        
        // Parse computed styles if available
        if (elem.isMember("computed_style")) {
            const Json::Value& style = elem["computed_style"];
            element.computed_style.display = style["display"].asString();
            element.computed_style.position = style["position"].asString();
            element.computed_style.flex_direction = style["flex_direction"].asString();
            element.computed_style.justify_content = style["justify_content"].asString();
            element.computed_style.align_items = style["align_items"].asString();
            element.computed_style.flex_grow = style["flex_grow"].asString();
            element.computed_style.flex_shrink = style["flex_shrink"].asString();
            element.computed_style.flex_basis = style["flex_basis"].asString();
            
            // Parse spacing values
            if (style.isMember("margin")) {
                const Json::Value& margin = style["margin"];
                element.computed_style.margin = {
                    margin["top"].asInt(),
                    margin["right"].asInt(),
                    margin["bottom"].asInt(),
                    margin["left"].asInt()
                };
            }
            
            if (style.isMember("padding")) {
                const Json::Value& padding = style["padding"];
                element.computed_style.padding = {
                    padding["top"].asInt(),
                    padding["right"].asInt(),
                    padding["bottom"].asInt(),
                    padding["left"].asInt()
                };
            }
            
            if (style.isMember("border")) {
                const Json::Value& border = style["border"];
                element.computed_style.border = {
                    border["top"].asInt(),
                    border["right"].asInt(),
                    border["bottom"].asInt(),
                    border["left"].asInt()
                };
            }
        }
        
        descriptor->expected_layout[selector] = element;
    }
    
    return descriptor;
}

LayoutTestResult BrowserLayoutValidator::validateLayout(
    const LayoutTestDescriptor& test_descriptor,
    ViewTree* radiant_view_tree
) {
    LayoutTestResult result;
    result.test_id = test_descriptor.test_id;
    result.passed = true;
    result.max_difference = 0.0;
    result.elements_tested = 0;
    result.elements_passed = 0;
    
    try {
        // Validate each expected element
        for (const auto& [selector, expected] : test_descriptor.expected_layout) {
            result.elements_tested++;
            
            // Find corresponding view in Radiant tree
            View* radiant_view = findViewBySelector(radiant_view_tree, selector);
            if (!radiant_view) {
                LayoutDifference diff;
                diff.element_selector = selector;
                diff.property_name = "existence";
                diff.expected_value = "element exists";
                diff.actual_value = "element not found";
                diff.difference = -1.0;
                diff.is_critical = true;
                result.differences.push_back(diff);
                result.passed = false;
                continue;
            }
            
            // Compare element properties
            auto element_diffs = compareElement(
                expected, 
                radiant_view, 
                test_descriptor.properties_to_test,
                test_descriptor.tolerance_px
            );
            
            bool element_passed = element_diffs.empty();
            if (element_passed) {
                result.elements_passed++;
            } else {
                result.passed = false;
                for (const auto& diff : element_diffs) {
                    result.differences.push_back(diff);
                    result.max_difference = std::max(result.max_difference, diff.difference);
                }
            }
        }
        
    } catch (const std::exception& e) {
        result.passed = false;
        result.error_message = e.what();
    }
    
    return result;
}

std::vector<LayoutDifference> BrowserLayoutValidator::compareElement(
    const ExpectedElement& expected,
    View* actual_view,
    const std::vector<std::string>& properties_to_test,
    double tolerance
) {
    std::vector<LayoutDifference> differences;
    
    // Extract actual layout from Radiant view
    LayoutRect actual_rect = extractLayoutRect(actual_view);
    
    // Check if we should test position and dimensions
    bool test_position = std::find(properties_to_test.begin(), properties_to_test.end(), "position") != properties_to_test.end();
    bool test_dimensions = std::find(properties_to_test.begin(), properties_to_test.end(), "dimensions") != properties_to_test.end();
    
    if (test_position || test_dimensions) {
        // Compare position
        if (test_position) {
            if (std::abs(expected.rect.x - actual_rect.x) > tolerance) {
                LayoutDifference diff;
                diff.element_selector = expected.selector;
                diff.property_name = "x";
                diff.expected_value = std::to_string(expected.rect.x);
                diff.actual_value = std::to_string(actual_rect.x);
                diff.difference = std::abs(expected.rect.x - actual_rect.x);
                diff.is_critical = diff.difference > tolerance * 2;
                differences.push_back(diff);
            }
            
            if (std::abs(expected.rect.y - actual_rect.y) > tolerance) {
                LayoutDifference diff;
                diff.element_selector = expected.selector;
                diff.property_name = "y";
                diff.expected_value = std::to_string(expected.rect.y);
                diff.actual_value = std::to_string(actual_rect.y);
                diff.difference = std::abs(expected.rect.y - actual_rect.y);
                diff.is_critical = diff.difference > tolerance * 2;
                differences.push_back(diff);
            }
        }
        
        // Compare dimensions
        if (test_dimensions) {
            if (std::abs(expected.rect.width - actual_rect.width) > tolerance) {
                LayoutDifference diff;
                diff.element_selector = expected.selector;
                diff.property_name = "width";
                diff.expected_value = std::to_string(expected.rect.width);
                diff.actual_value = std::to_string(actual_rect.width);
                diff.difference = std::abs(expected.rect.width - actual_rect.width);
                diff.is_critical = diff.difference > tolerance * 2;
                differences.push_back(diff);
            }
            
            if (std::abs(expected.rect.height - actual_rect.height) > tolerance) {
                LayoutDifference diff;
                diff.element_selector = expected.selector;
                diff.property_name = "height";
                diff.expected_value = std::to_string(expected.rect.height);
                diff.actual_value = std::to_string(actual_rect.height);
                diff.difference = std::abs(expected.rect.height - actual_rect.height);
                diff.is_critical = diff.difference > tolerance * 2;
                differences.push_back(diff);
            }
        }
    }
    
    // TODO: Add flex property comparisons, computed style validation, etc.
    
    return differences;
}

View* BrowserLayoutValidator::findViewBySelector(ViewTree* tree, const std::string& selector) {
    if (!tree || !tree->root) return nullptr;
    
    // Simplified selector matching - could be enhanced
    // For now, match by class names, IDs, or element indices
    
    std::function<View*(View*, const std::string&, int&)> findView = 
        [&](View* view, const std::string& sel, int& index) -> View* {
        if (!view) return nullptr;
        
        // Simple class matching (.classname)
        if (sel.starts_with(".")) {
            std::string className = sel.substr(1);
            // Check if view has this class (simplified - would need proper CSS class parsing)
            if (view->node && view->node->has_class(className.c_str())) {
                return view;
            }
        }
        // Simple ID matching (#id)
        else if (sel.starts_with("#")) {
            std::string id = sel.substr(1);
            if (view->node && view->node->get_id() == id) {
                return view;
            }
        }
        // Simple tag[index] matching
        else if (sel.find("[") != std::string::npos) {
            std::regex pattern(R"((\w+)\[(\d+)\])");
            std::smatch matches;
            if (std::regex_match(sel, matches, pattern)) {
                std::string tag = matches[1].str();
                int targetIndex = std::stoi(matches[2].str());
                if (view->node && view->node->tag_name() == tag && index == targetIndex) {
                    return view;
                }
                if (view->node && view->node->tag_name() == tag) {
                    index++;
                }
            }
        }
        
        // Recursively search children
        if (view->type >= RDT_VIEW_INLINE) {
            ViewGroup* group = (ViewGroup*)view;
            View* child = group->child;
            while (child) {
                View* found = findView(child, sel, index);
                if (found) return found;
                child = child->next;
            }
        }
        
        return nullptr;
    };
    
    int index = 0;
    return findView(tree->root, selector, index);
}

LayoutRect BrowserLayoutValidator::extractLayoutRect(View* view) {
    LayoutRect rect = {0, 0, 0, 0};
    
    if (!view) return rect;
    
    // Extract position and dimensions based on view type
    switch (view->type) {
        case RDT_VIEW_BLOCK:
        case RDT_VIEW_INLINE_BLOCK: {
            ViewBlock* block = (ViewBlock*)view;
            rect.x = block->x;
            rect.y = block->y;
            rect.width = block->width;
            rect.height = block->height;
            break;
        }
        case RDT_VIEW_INLINE: {
            ViewSpan* span = (ViewSpan*)view;
            rect.x = span->x;
            rect.y = span->y;
            rect.width = span->width;
            rect.height = span->height;
            break;
        }
        case RDT_VIEW_TEXT: {
            ViewText* text = (ViewText*)view;
            rect.x = text->x;
            rect.y = text->y;
            rect.width = text->width;
            rect.height = text->height;
            break;
        }
        default:
            // Generic view - try to get basic position
            rect.x = view->x;
            rect.y = view->y;
            break;
    }
    
    return rect;
}

void BrowserLayoutValidator::generateTestReport(
    const std::vector<LayoutTestResult>& results,
    const std::string& output_file
) {
    std::ofstream report(output_file);
    if (!report.is_open()) {
        throw std::runtime_error("Failed to create report file: " + output_file);
    }
    
    // Generate HTML report
    report << R"(<!DOCTYPE html>
<html>
<head>
    <title>Radiant Layout Test Report</title>
    <style>
        body { font-family: Arial, sans-serif; margin: 20px; }
        .summary { background: #f0f0f0; padding: 15px; border-radius: 5px; margin-bottom: 20px; }
        .test-result { border: 1px solid #ddd; margin: 10px 0; padding: 15px; border-radius: 5px; }
        .passed { border-left: 5px solid #4CAF50; }
        .failed { border-left: 5px solid #f44336; }
        .difference { margin: 5px 0; padding: 8px; background: #fff3cd; border-radius: 3px; }
        .critical { background: #f8d7da; }
        table { width: 100%; border-collapse: collapse; margin: 10px 0; }
        th, td { padding: 8px; text-align: left; border-bottom: 1px solid #ddd; }
        th { background-color: #f2f2f2; }
    </style>
</head>
<body>
    <h1>Radiant Layout Test Report</h1>
)";
    
    // Summary
    int total_tests = results.size();
    int passed_tests = 0;
    int total_elements = 0;
    int passed_elements = 0;
    
    for (const auto& result : results) {
        if (result.passed) passed_tests++;
        total_elements += result.elements_tested;
        passed_elements += result.elements_passed;
    }
    
    report << "<div class='summary'>\n";
    report << "<h2>Test Summary</h2>\n";
    report << "<p><strong>Tests:</strong> " << passed_tests << "/" << total_tests << " passed (" 
           << (100 * passed_tests / total_tests) << "%)</p>\n";
    report << "<p><strong>Elements:</strong> " << passed_elements << "/" << total_elements << " passed (" 
           << (100 * passed_elements / total_elements) << "%)</p>\n";
    report << "</div>\n";
    
    // Individual test results
    for (const auto& result : results) {
        std::string status_class = result.passed ? "passed" : "failed";
        report << "<div class='test-result " << status_class << "'>\n";
        report << "<h3>" << result.test_id << "</h3>\n";
        report << "<p><strong>Status:</strong> " << (result.passed ? "PASSED" : "FAILED") << "</p>\n";
        report << "<p><strong>Elements tested:</strong> " << result.elements_tested << "</p>\n";
        report << "<p><strong>Elements passed:</strong> " << result.elements_passed << "</p>\n";
        
        if (!result.error_message.empty()) {
            report << "<p><strong>Error:</strong> " << result.error_message << "</p>\n";
        }
        
        if (!result.differences.empty()) {
            report << "<h4>Differences:</h4>\n";
            for (const auto& diff : result.differences) {
                std::string diff_class = diff.is_critical ? "difference critical" : "difference";
                report << "<div class='" << diff_class << "'>\n";
                report << "<strong>" << diff.element_selector << " - " << diff.property_name << ":</strong> ";
                report << "Expected " << diff.expected_value << ", got " << diff.actual_value;
                report << " (difference: " << diff.difference << "px)\n";
                report << "</div>\n";
            }
        }
        
        report << "</div>\n";
    }
    
    report << "</body></html>";
    report.close();
}

// RadiantBrowserTestSuite implementation
std::vector<LayoutTestResult> RadiantBrowserTestSuite::runTestDirectory(
    const std::string& test_dir,
    UiContext* ui_context
) {
    std::vector<LayoutTestResult> results;
    
    // TODO: Implement directory traversal and test execution
    // This would scan for .json test files, load each one, and run the tests
    
    return results;
}

LayoutTestResult RadiantBrowserTestSuite::runSingleTest(
    const std::string& test_file,
    UiContext* ui_context
) {
    // Load test descriptor
    auto descriptor = BrowserLayoutValidator::loadTestDescriptor(test_file);
    
    // Create HTML document from test data
    std::string full_html = "<!DOCTYPE html><html><head><style>" + 
                           descriptor->css + "</style></head><body>" + 
                           descriptor->html + "</body></html>";
    
    // Parse and layout the document using Radiant
    // TODO: Integrate with Radiant's HTML parser and layout engine
    Document* doc = nullptr; // Would create document from HTML string
    // layout_html_doc(ui_context, doc, false);
    
    // Validate against browser reference
    if (doc && doc->view_tree) {
        return BrowserLayoutValidator::validateLayout(*descriptor, doc->view_tree);
    } else {
        LayoutTestResult result;
        result.test_id = descriptor->test_id;
        result.passed = false;
        result.error_message = "Failed to create or layout document";
        return result;
    }
}

// Utility functions
namespace utils {

std::string normalizeCssSelector(const std::string& selector) {
    // Remove whitespace and normalize selector format
    std::string normalized = selector;
    normalized.erase(std::remove_if(normalized.begin(), normalized.end(), ::isspace), normalized.end());
    return normalized;
}

bool compareProperty(const std::string& expected, const std::string& actual, double tolerance) {
    // Try numeric comparison first
    try {
        double exp_val = extractNumericValue(expected);
        double act_val = extractNumericValue(actual);
        return std::abs(exp_val - act_val) <= tolerance;
    } catch (...) {
        // Fall back to string comparison
        return expected == actual;
    }
}

double extractNumericValue(const std::string& css_value) {
    std::regex number_regex(R"((-?\d+(?:\.\d+)?)(?:px|em|rem|%|pt|pc|in|cm|mm|ex|ch|vw|vh|vmin|vmax)?)");
    std::smatch matches;
    
    if (std::regex_search(css_value, matches, number_regex)) {
        return std::stod(matches[1].str());
    }
    
    throw std::invalid_argument("No numeric value found in: " + css_value);
}

} // namespace utils

} // namespace testing
} // namespace radiant
