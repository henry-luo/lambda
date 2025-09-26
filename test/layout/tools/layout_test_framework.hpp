#pragma once

/**
 * Layout Test Framework for Radiant Engine
 * 
 * Provides infrastructure for automated testing of Radiant's layout engine
 * against browser-extracted reference data.
 */

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <functional>
#include <optional>
#include <iostream>
#include <fstream>
#include <algorithm>
#include <cmath>

// JSON library for parsing reference data
#include <json-c/json.h>

// Radiant layout system headers
// (These paths may need adjustment based on actual Radiant structure)
#ifdef INCLUDE_RADIANT_HEADERS
#include "../../radiant/layout/layout.hpp"
#include "../../radiant/layout/layout_block.hpp"
#include "../../radiant/layout/layout_flex.hpp"
#include "../../radiant/dom/dom_element.hpp"
#endif

namespace LayoutTest {

// Forward declarations
struct LayoutProperties;
struct ElementData;
struct TestCase;
struct ValidationResult;
class LayoutValidator;
class TestRunner;

/**
 * Layout properties extracted from browser or computed by Radiant
 */
struct LayoutProperties {
    // Position and dimensions
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;
    
    // Content dimensions
    double contentWidth = 0.0;
    double contentHeight = 0.0;
    
    // Box model
    double marginTop = 0.0, marginRight = 0.0, marginBottom = 0.0, marginLeft = 0.0;
    double paddingTop = 0.0, paddingRight = 0.0, paddingBottom = 0.0, paddingLeft = 0.0;
    double borderTop = 0.0, borderRight = 0.0, borderBottom = 0.0, borderLeft = 0.0;
    
    // CSS properties
    std::string display = "block";
    std::string position = "static";
    std::string flexDirection = "row";
    std::string flexWrap = "nowrap";
    std::string justifyContent = "flex-start";
    std::string alignItems = "stretch";
    std::string alignContent = "stretch";
    std::string alignSelf = "auto";
    double flexGrow = 0.0;
    double flexShrink = 1.0;
    std::string flexBasis = "auto";
    int order = 0;
    
    // Typography
    double fontSize = 16.0;
    std::string fontFamily = "serif";
    std::string fontWeight = "normal";
    std::string textAlign = "left";
    
    // Computed values
    std::string width_css = "auto";
    std::string height_css = "auto";
    std::string minWidth = "0px";
    std::string maxWidth = "none";
    std::string minHeight = "0px";
    std::string maxHeight = "none";
    
    // Helper methods
    double totalMarginHorizontal() const { return marginLeft + marginRight; }
    double totalMarginVertical() const { return marginTop + marginBottom; }
    double totalPaddingHorizontal() const { return paddingLeft + paddingRight; }
    double totalPaddingVertical() const { return paddingTop + paddingBottom; }
    double totalBorderHorizontal() const { return borderLeft + borderRight; }
    double totalBorderVertical() const { return borderTop + borderBottom; }
    
    double outerWidth() const { 
        return width + totalPaddingHorizontal() + totalBorderHorizontal() + totalMarginHorizontal(); 
    }
    double outerHeight() const { 
        return height + totalPaddingVertical() + totalBorderVertical() + totalMarginVertical(); 
    }
};

/**
 * Element data including layout properties and metadata
 */
struct ElementData {
    std::string selector;
    std::string tag;
    std::string id;
    std::vector<std::string> classes;
    
    LayoutProperties layout;
    LayoutProperties computed; // Browser-computed reference
    
    std::string textContent;
    bool hasTextNodes = false;
    int childCount = 0;
    std::string parentSelector;
    
    // Validation helpers
    bool isFlexContainer() const { return layout.display == "flex"; }
    bool isBlockElement() const { return layout.display == "block"; }
    bool isInlineElement() const { return layout.display == "inline" || layout.display == "inline-block"; }
    bool hasFixedWidth() const { return layout.width_css != "auto" && layout.width_css.find('%') == std::string::npos; }
    bool hasFixedHeight() const { return layout.height_css != "auto" && layout.height_css.find('%') == std::string::npos; }
};

/**
 * Test case containing HTML/CSS and expected browser reference
 */
struct TestCase {
    std::string name;
    std::string category; // basic, intermediate, advanced
    std::string htmlFile;
    std::string referenceFile;
    
    // Test metadata
    std::map<std::string, std::string> metadata;
    std::vector<std::string> features; // e.g., "flexbox", "block-layout", "margins"
    std::string description;
    
    // Loaded data
    std::string htmlContent;
    std::map<std::string, ElementData> referenceElements;
    
    // Test configuration
    double tolerancePixels = 1.0; // Pixel tolerance for layout comparisons
    double tolerancePercent = 0.01; // Percentage tolerance for relative measurements
    std::vector<std::string> ignoreSelectors; // Elements to skip during validation
    
    bool isLoaded() const { return !htmlContent.empty() && !referenceElements.empty(); }
};

/**
 * Validation result for a single element or test case
 */
struct ValidationResult {
    enum class Status { PASS, FAIL, SKIP, ERROR };
    
    Status status = Status::PASS;
    std::string message;
    std::string selector;
    
    // Detailed comparison data
    struct PropertyComparison {
        std::string property;
        double expected = 0.0;
        double actual = 0.0;
        double difference = 0.0;
        double tolerance = 0.0;
        bool withinTolerance = true;
        std::string unit = "px";
    };
    
    std::vector<PropertyComparison> propertyComparisons;
    
    // Summary statistics
    int totalProperties = 0;
    int passedProperties = 0;
    int failedProperties = 0;
    
    bool isPass() const { return status == Status::PASS; }
    bool isFail() const { return status == Status::FAIL; }
    bool isSkip() const { return status == Status::SKIP; }
    bool isError() const { return status == Status::ERROR; }
    
    double successRate() const {
        return totalProperties > 0 ? (double)passedProperties / totalProperties : 1.0;
    }
};

/**
 * Layout validator for comparing Radiant output against browser references
 */
class LayoutValidator {
public:
    LayoutValidator();
    ~LayoutValidator();
    
    // Configuration
    void setTolerancePixels(double pixels) { defaultTolerancePixels_ = pixels; }
    void setTolerancePercent(double percent) { defaultTolerancePercent_ = percent; }
    void addIgnoreProperty(const std::string& property) { ignoreProperties_.push_back(property); }
    void addIgnoreSelector(const std::string& selector) { ignoreSelectors_.push_back(selector); }
    
    // Load test data
    bool loadTestCase(TestCase& testCase);
    bool loadReferenceData(const std::string& jsonFile, std::map<std::string, ElementData>& elements);
    
    // Validation methods
    ValidationResult validateElement(
        const std::string& selector,
        const ElementData& reference,
        const ElementData& actual
    );
    
    ValidationResult validateTestCase(
        const TestCase& testCase,
        const std::map<std::string, ElementData>& actualElements
    );
    
    // Property-specific validation
    ValidationResult::PropertyComparison compareProperty(
        const std::string& property,
        double expected,
        double actual,
        double tolerance = -1.0
    );
    
    ValidationResult::PropertyComparison compareStringProperty(
        const std::string& property,
        const std::string& expected,
        const std::string& actual
    );
    
    // Utility methods
    bool isWithinTolerance(double expected, double actual, double tolerance) const;
    double calculateTolerance(double value, double percentTolerance, double pixelTolerance) const;
    std::string formatDifference(double difference) const;
    
private:
    double defaultTolerancePixels_ = 1.0;
    double defaultTolerancePercent_ = 0.01;
    std::vector<std::string> ignoreProperties_;
    std::vector<std::string> ignoreSelectors_;
    
    // JSON parsing helpers
    bool parseElementFromJson(json_object* obj, ElementData& element);
    bool parseLayoutPropertiesFromJson(json_object* obj, LayoutProperties& props);
    double getDoubleFromJson(json_object* obj, const char* key, double defaultValue = 0.0);
    std::string getStringFromJson(json_object* obj, const char* key, const std::string& defaultValue = "");
    
    // Validation helpers
    bool shouldIgnoreSelector(const std::string& selector) const;
    bool shouldIgnoreProperty(const std::string& property) const;
    std::vector<std::string> getValidationProperties() const;
};

/**
 * Test runner for executing layout validation tests
 */
class TestRunner {
public:
    TestRunner();
    ~TestRunner();
    
    // Configuration
    void setOutputDirectory(const std::string& dir) { outputDirectory_ = dir; }
    void setVerbose(bool verbose) { verbose_ = verbose; }
    void setStopOnFirstFailure(bool stop) { stopOnFirstFailure_ = stop; }
    
    // Test discovery and loading
    std::vector<TestCase> discoverTests(const std::string& testDirectory);
    std::vector<TestCase> loadCategory(const std::string& category);
    bool loadTestCase(TestCase& testCase);
    
    // Test execution
    struct TestSuiteResults {
        std::vector<ValidationResult> results;
        int totalTests = 0;
        int passedTests = 0;
        int failedTests = 0;
        int skippedTests = 0;
        int errorTests = 0;
        double totalTime = 0.0;
        std::string category;
    };
    
    TestSuiteResults runCategory(const std::string& category);
    TestSuiteResults runAllTests();
    ValidationResult runSingleTest(const TestCase& testCase);
    
    // Reporting
    void generateReport(const TestSuiteResults& results, const std::string& outputFile);
    void generateHtmlReport(const TestSuiteResults& results, const std::string& outputFile);
    void generateJsonReport(const TestSuiteResults& results, const std::string& outputFile);
    void printSummary(const TestSuiteResults& results);
    
    // Integration with Radiant engine
    #ifdef INCLUDE_RADIANT_HEADERS
    std::map<std::string, ElementData> runRadiantLayout(const TestCase& testCase);
    ElementData extractRadiantElementData(const RadiantLayout::Element* element);
    #endif
    
private:
    std::string outputDirectory_ = "./reports";
    bool verbose_ = false;
    bool stopOnFirstFailure_ = false;
    
    std::unique_ptr<LayoutValidator> validator_;
    
    // Utility methods
    std::string getCurrentTimestamp() const;
    std::string formatDuration(double seconds) const;
    std::string escapeHtml(const std::string& text) const;
    void ensureDirectoryExists(const std::string& dir);
};

// Utility functions
namespace Utils {
    /**
     * Parse CSS length value (e.g., "10px", "50%", "auto")
     */
    struct CssLength {
        double value = 0.0;
        std::string unit = "px";
        bool isAuto = false;
        bool isNone = false;
        
        static CssLength parse(const std::string& css);
        double toPixels(double containerSize = 0.0, double fontSize = 16.0) const;
        std::string toString() const;
    };
    
    /**
     * CSS color parsing and comparison
     */
    struct CssColor {
        int r = 0, g = 0, b = 0, a = 255;
        
        static CssColor parse(const std::string& css);
        std::string toHex() const;
        bool equals(const CssColor& other, int tolerance = 0) const;
    };
    
    /**
     * String utilities
     */
    std::vector<std::string> split(const std::string& str, char delimiter);
    std::string trim(const std::string& str);
    std::string toLowerCase(const std::string& str);
    bool startsWith(const std::string& str, const std::string& prefix);
    bool endsWith(const std::string& str, const std::string& suffix);
    
    /**
     * File utilities
     */
    bool fileExists(const std::string& path);
    std::string readFile(const std::string& path);
    bool writeFile(const std::string& path, const std::string& content);
    std::vector<std::string> listFiles(const std::string& directory, const std::string& extension = "");
    
    /**
     * Math utilities
     */
    double roundToDecimalPlaces(double value, int places);
    bool isNearlyEqual(double a, double b, double epsilon = 1e-6);
    double clamp(double value, double min, double max);
}

} // namespace LayoutTest
