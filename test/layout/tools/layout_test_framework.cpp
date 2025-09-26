/**
 * Layout Test Framework Implementation
 * 
 * Implementation of the layout testing framework for Radiant engine validation.
 */

#include "layout_test_framework.hpp"
#include <chrono>
#include <iomanip>
#include <sstream>
#include <regex>
#include <filesystem>

namespace LayoutTest {

// LayoutValidator Implementation
LayoutValidator::LayoutValidator() {
    // Initialize with sensible defaults
    defaultTolerancePixels_ = 1.0;
    defaultTolerancePercent_ = 0.01;
    
    // Properties that are often inconsistent across browsers/engines
    ignoreProperties_ = {
        "scrollWidth", "scrollHeight",  // Scrolling varies by implementation
        "fontFamily",                   // Font rendering differences
        "userAgent"                     // Browser-specific
    };
}

LayoutValidator::~LayoutValidator() = default;

bool LayoutValidator::loadTestCase(TestCase& testCase) {
    // Load HTML content
    testCase.htmlContent = Utils::readFile(testCase.htmlFile);
    if (testCase.htmlContent.empty()) {
        std::cerr << "Failed to load HTML file: " << testCase.htmlFile << std::endl;
        return false;
    }
    
    // Load reference data
    if (!loadReferenceData(testCase.referenceFile, testCase.referenceElements)) {
        std::cerr << "Failed to load reference data: " << testCase.referenceFile << std::endl;
        return false;
    }
    
    return true;
}

bool LayoutValidator::loadReferenceData(const std::string& jsonFile, std::map<std::string, ElementData>& elements) {
    std::string jsonContent = Utils::readFile(jsonFile);
    if (jsonContent.empty()) {
        return false;
    }
    
    json_object* root = json_tokener_parse(jsonContent.c_str());
    if (!root) {
        std::cerr << "Failed to parse JSON: " << jsonFile << std::endl;
        return false;
    }
    
    // Get layout_data object
    json_object* layoutData;
    if (!json_object_object_get_ex(root, "layout_data", &layoutData)) {
        std::cerr << "No layout_data found in JSON: " << jsonFile << std::endl;
        json_object_put(root);
        return false;
    }
    
    // Parse each element
    json_object_object_foreach(layoutData, selector, elementObj) {
        // Skip special objects
        if (std::string(selector).substr(0, 2) == "__") {
            continue;
        }
        
        ElementData element;
        element.selector = selector;
        
        if (parseElementFromJson(elementObj, element)) {
            elements[selector] = element;
        }
    }
    
    json_object_put(root);
    return !elements.empty();
}

bool LayoutValidator::parseElementFromJson(json_object* obj, ElementData& element) {
    // Basic element info
    element.tag = getStringFromJson(obj, "tag");
    element.id = getStringFromJson(obj, "id");
    element.textContent = getStringFromJson(obj, "textContent");
    element.hasTextNodes = json_object_get_boolean(json_object_object_get(obj, "hasTextNodes"));
    element.childCount = json_object_get_int(json_object_object_get(obj, "childCount"));
    element.parentSelector = getStringFromJson(obj, "parentSelector");
    
    // Parse classes array
    json_object* classesArray;
    if (json_object_object_get_ex(obj, "classes", &classesArray)) {
        int arrayLen = json_object_array_length(classesArray);
        for (int i = 0; i < arrayLen; i++) {
            json_object* classObj = json_object_array_get_idx(classesArray, i);
            element.classes.push_back(json_object_get_string(classObj));
        }
    }
    
    // Parse layout properties
    json_object* layoutObj;
    if (json_object_object_get_ex(obj, "layout", &layoutObj)) {
        element.layout.x = getDoubleFromJson(layoutObj, "x");
        element.layout.y = getDoubleFromJson(layoutObj, "y");
        element.layout.width = getDoubleFromJson(layoutObj, "width");
        element.layout.height = getDoubleFromJson(layoutObj, "height");
        element.layout.contentWidth = getDoubleFromJson(layoutObj, "contentWidth");
        element.layout.contentHeight = getDoubleFromJson(layoutObj, "contentHeight");
    }
    
    // Parse computed CSS properties
    json_object* computedObj;
    if (json_object_object_get_ex(obj, "computed", &computedObj)) {
        parseLayoutPropertiesFromJson(computedObj, element.computed);
    }
    
    return true;
}

bool LayoutValidator::parseLayoutPropertiesFromJson(json_object* obj, LayoutProperties& props) {
    // Display and positioning
    props.display = getStringFromJson(obj, "display", "block");
    props.position = getStringFromJson(obj, "position", "static");
    
    // Box model
    props.marginTop = getDoubleFromJson(obj, "marginTop");
    props.marginRight = getDoubleFromJson(obj, "marginRight");
    props.marginBottom = getDoubleFromJson(obj, "marginBottom");
    props.marginLeft = getDoubleFromJson(obj, "marginLeft");
    
    props.paddingTop = getDoubleFromJson(obj, "paddingTop");
    props.paddingRight = getDoubleFromJson(obj, "paddingRight");
    props.paddingBottom = getDoubleFromJson(obj, "paddingBottom");
    props.paddingLeft = getDoubleFromJson(obj, "paddingLeft");
    
    props.borderTop = getDoubleFromJson(obj, "borderTopWidth");
    props.borderRight = getDoubleFromJson(obj, "borderRightWidth");
    props.borderBottom = getDoubleFromJson(obj, "borderBottomWidth");
    props.borderLeft = getDoubleFromJson(obj, "borderLeftWidth");
    
    // Flexbox properties
    props.flexDirection = getStringFromJson(obj, "flexDirection", "row");
    props.flexWrap = getStringFromJson(obj, "flexWrap", "nowrap");
    props.justifyContent = getStringFromJson(obj, "justifyContent", "flex-start");
    props.alignItems = getStringFromJson(obj, "alignItems", "stretch");
    props.alignContent = getStringFromJson(obj, "alignContent", "stretch");
    props.alignSelf = getStringFromJson(obj, "alignSelf", "auto");
    props.flexGrow = getDoubleFromJson(obj, "flexGrow");
    props.flexShrink = getDoubleFromJson(obj, "flexShrink", 1.0);
    props.flexBasis = getStringFromJson(obj, "flexBasis", "auto");
    props.order = json_object_get_int(json_object_object_get(obj, "order"));
    
    // Typography
    props.fontSize = getDoubleFromJson(obj, "fontSize", 16.0);
    props.fontFamily = getStringFromJson(obj, "fontFamily", "serif");
    props.fontWeight = getStringFromJson(obj, "fontWeight", "normal");
    props.textAlign = getStringFromJson(obj, "textAlign", "left");
    
    // Sizing
    props.width_css = getStringFromJson(obj, "width", "auto");
    props.height_css = getStringFromJson(obj, "height", "auto");
    props.minWidth = getStringFromJson(obj, "minWidth", "0px");
    props.maxWidth = getStringFromJson(obj, "maxWidth", "none");
    props.minHeight = getStringFromJson(obj, "minHeight", "0px");
    props.maxHeight = getStringFromJson(obj, "maxHeight", "none");
    
    return true;
}

double LayoutValidator::getDoubleFromJson(json_object* obj, const char* key, double defaultValue) {
    json_object* valueObj;
    if (json_object_object_get_ex(obj, key, &valueObj)) {
        return json_object_get_double(valueObj);
    }
    return defaultValue;
}

std::string LayoutValidator::getStringFromJson(json_object* obj, const char* key, const std::string& defaultValue) {
    json_object* valueObj;
    if (json_object_object_get_ex(obj, key, &valueObj)) {
        const char* str = json_object_get_string(valueObj);
        return str ? std::string(str) : defaultValue;
    }
    return defaultValue;
}

ValidationResult LayoutValidator::validateElement(
    const std::string& selector,
    const ElementData& reference,
    const ElementData& actual
) {
    ValidationResult result;
    result.selector = selector;
    result.status = ValidationResult::Status::PASS;
    
    if (shouldIgnoreSelector(selector)) {
        result.status = ValidationResult::Status::SKIP;
        result.message = "Selector in ignore list";
        return result;
    }
    
    // List of numeric properties to validate
    std::vector<std::pair<std::string, std::pair<double, double>>> numericComparisons = {
        {"x", {reference.layout.x, actual.layout.x}},
        {"y", {reference.layout.y, actual.layout.y}},
        {"width", {reference.layout.width, actual.layout.width}},
        {"height", {reference.layout.height, actual.layout.height}},
        {"contentWidth", {reference.layout.contentWidth, actual.layout.contentWidth}},
        {"contentHeight", {reference.layout.contentHeight, actual.layout.contentHeight}},
        
        {"marginTop", {reference.computed.marginTop, actual.computed.marginTop}},
        {"marginRight", {reference.computed.marginRight, actual.computed.marginRight}},
        {"marginBottom", {reference.computed.marginBottom, actual.computed.marginBottom}},
        {"marginLeft", {reference.computed.marginLeft, actual.computed.marginLeft}},
        
        {"paddingTop", {reference.computed.paddingTop, actual.computed.paddingTop}},
        {"paddingRight", {reference.computed.paddingRight, actual.computed.paddingRight}},
        {"paddingBottom", {reference.computed.paddingBottom, actual.computed.paddingBottom}},
        {"paddingLeft", {reference.computed.paddingLeft, actual.computed.paddingLeft}},
        
        {"flexGrow", {reference.computed.flexGrow, actual.computed.flexGrow}},
        {"flexShrink", {reference.computed.flexShrink, actual.computed.flexShrink}},
        {"fontSize", {reference.computed.fontSize, actual.computed.fontSize}}
    };
    
    // Validate numeric properties
    for (const auto& comparison : numericComparisons) {
        if (shouldIgnoreProperty(comparison.first)) continue;
        
        auto propertyResult = compareProperty(
            comparison.first,
            comparison.second.first,
            comparison.second.second
        );
        
        result.propertyComparisons.push_back(propertyResult);
        result.totalProperties++;
        
        if (propertyResult.withinTolerance) {
            result.passedProperties++;
        } else {
            result.failedProperties++;
            result.status = ValidationResult::Status::FAIL;
        }
    }
    
    // Validate string properties
    std::vector<std::pair<std::string, std::pair<std::string, std::string>>> stringComparisons = {
        {"display", {reference.computed.display, actual.computed.display}},
        {"position", {reference.computed.position, actual.computed.position}},
        {"flexDirection", {reference.computed.flexDirection, actual.computed.flexDirection}},
        {"flexWrap", {reference.computed.flexWrap, actual.computed.flexWrap}},
        {"justifyContent", {reference.computed.justifyContent, actual.computed.justifyContent}},
        {"alignItems", {reference.computed.alignItems, actual.computed.alignItems}},
        {"textAlign", {reference.computed.textAlign, actual.computed.textAlign}}
    };
    
    for (const auto& comparison : stringComparisons) {
        if (shouldIgnoreProperty(comparison.first)) continue;
        
        auto propertyResult = compareStringProperty(
            comparison.first,
            comparison.second.first,
            comparison.second.second
        );
        
        // Convert string comparison result to numeric format for consistency
        ValidationResult::PropertyComparison numericResult;
        numericResult.property = propertyResult.property;
        numericResult.expected = propertyResult.expected;
        numericResult.actual = propertyResult.actual;
        numericResult.withinTolerance = propertyResult.withinTolerance;
        numericResult.unit = "string";
        
        result.propertyComparisons.push_back(numericResult);
        result.totalProperties++;
        
        if (numericResult.withinTolerance) {
            result.passedProperties++;
        } else {
            result.failedProperties++;
            result.status = ValidationResult::Status::FAIL;
        }
    }
    
    // Generate summary message
    if (result.status == ValidationResult::Status::PASS) {
        result.message = "All properties match within tolerance";
    } else {
        std::ostringstream oss;
        oss << result.failedProperties << " of " << result.totalProperties 
            << " properties failed validation";
        result.message = oss.str();
    }
    
    return result;
}

ValidationResult::PropertyComparison LayoutValidator::compareProperty(
    const std::string& property,
    double expected,
    double actual,
    double tolerance
) {
    ValidationResult::PropertyComparison result;
    result.property = property;
    result.expected = expected;
    result.actual = actual;
    result.difference = std::abs(actual - expected);
    
    if (tolerance < 0) {
        result.tolerance = calculateTolerance(expected, defaultTolerancePercent_, defaultTolerancePixels_);
    } else {
        result.tolerance = tolerance;
    }
    
    result.withinTolerance = isWithinTolerance(expected, actual, result.tolerance);
    result.unit = "px";
    
    return result;
}

ValidationResult::PropertyComparison LayoutValidator::compareStringProperty(
    const std::string& property,
    const std::string& expected,
    const std::string& actual
) {
    ValidationResult::PropertyComparison result;
    result.property = property;
    // Store strings in expected/actual as dummy numeric values for interface consistency
    result.expected = 0.0;
    result.actual = expected == actual ? 0.0 : 1.0;
    result.difference = 0.0;
    result.tolerance = 0.0;
    result.withinTolerance = (expected == actual);
    result.unit = "string";
    
    return result;
}

bool LayoutValidator::isWithinTolerance(double expected, double actual, double tolerance) const {
    return std::abs(actual - expected) <= tolerance;
}

double LayoutValidator::calculateTolerance(double value, double percentTolerance, double pixelTolerance) const {
    double percentTol = std::abs(value) * percentTolerance;
    return (percentTol > pixelTolerance) ? percentTol : pixelTolerance;
}

bool LayoutValidator::shouldIgnoreSelector(const std::string& selector) const {
    return std::find(ignoreSelectors_.begin(), ignoreSelectors_.end(), selector) != ignoreSelectors_.end();
}

bool LayoutValidator::shouldIgnoreProperty(const std::string& property) const {
    return std::find(ignoreProperties_.begin(), ignoreProperties_.end(), property) != ignoreProperties_.end();
}

// TestRunner Implementation
TestRunner::TestRunner() {
    validator_ = std::make_unique<LayoutValidator>();
}

TestRunner::~TestRunner() = default;

std::vector<TestCase> TestRunner::discoverTests(const std::string& testDirectory) {
    std::vector<TestCase> tests;
    
    std::vector<std::string> categories = {"basic", "intermediate", "advanced"};
    
    for (const std::string& category : categories) {
        auto categoryTests = loadCategory(category);
        tests.insert(tests.end(), categoryTests.begin(), categoryTests.end());
    }
    
    return tests;
}

std::vector<TestCase> TestRunner::loadCategory(const std::string& category) {
    std::vector<TestCase> tests;
    
    std::string dataDir = "./data/" + category;
    std::string referenceDir = "./reference/" + category;
    
    if (!std::filesystem::exists(dataDir) || !std::filesystem::exists(referenceDir)) {
        std::cerr << "Warning: Missing directories for category " << category << std::endl;
        return tests;
    }
    
    // Find HTML files
    auto htmlFiles = Utils::listFiles(dataDir, ".html");
    
    for (const std::string& htmlFile : htmlFiles) {
        std::string baseName = std::filesystem::path(htmlFile).stem().string();
        std::string referenceFile = referenceDir + "/" + baseName + ".json";
        
        if (Utils::fileExists(referenceFile)) {
            TestCase testCase;
            testCase.name = baseName;
            testCase.category = category;
            testCase.htmlFile = htmlFile;
            testCase.referenceFile = referenceFile;
            testCase.description = "Auto-generated test case for " + baseName;
            
            tests.push_back(testCase);
        }
    }
    
    return tests;
}

TestRunner::TestSuiteResults TestRunner::runCategory(const std::string& category) {
    TestSuiteResults results;
    results.category = category;
    
    auto startTime = std::chrono::high_resolution_clock::now();
    
    auto tests = loadCategory(category);
    results.totalTests = tests.size();
    
    if (verbose_) {
        std::cout << "Running " << tests.size() << " tests in category: " << category << std::endl;
    }
    
    for (const auto& testCase : tests) {
        if (verbose_) {
            std::cout << "  Running: " << testCase.name << "..." << std::flush;
        }
        
        auto testResult = runSingleTest(testCase);
        results.results.push_back(testResult);
        
        switch (testResult.status) {
            case ValidationResult::Status::PASS:
                results.passedTests++;
                if (verbose_) std::cout << " PASS" << std::endl;
                break;
            case ValidationResult::Status::FAIL:
                results.failedTests++;
                if (verbose_) std::cout << " FAIL" << std::endl;
                break;
            case ValidationResult::Status::SKIP:
                results.skippedTests++;
                if (verbose_) std::cout << " SKIP" << std::endl;
                break;
            case ValidationResult::Status::ERROR:
                results.errorTests++;
                if (verbose_) std::cout << " ERROR" << std::endl;
                break;
        }
        
        if (stopOnFirstFailure_ && testResult.isFail()) {
            break;
        }
    }
    
    auto endTime = std::chrono::high_resolution_clock::now();
    results.totalTime = std::chrono::duration<double>(endTime - startTime).count();
    
    return results;
}

ValidationResult TestRunner::runSingleTest(const TestCase& testCase) {
    ValidationResult result;
    result.selector = testCase.name;
    
    // This is where we would integrate with Radiant's layout engine
    // For now, return a placeholder result
    
    try {
        // Load test case if not already loaded
        TestCase mutableTestCase = testCase;
        if (!mutableTestCase.isLoaded()) {
            if (!validator_->loadTestCase(mutableTestCase)) {
                result.status = ValidationResult::Status::ERROR;
                result.message = "Failed to load test case";
                return result;
            }
        }
        
        // Integration with Radiant layout engine
        std::map<std::string, ElementData> actualElements = runRadiantLayout(mutableTestCase);
        
        result = validator_->validateTestCase(mutableTestCase, actualElements);
        
    } catch (const std::exception& e) {
        result.status = ValidationResult::Status::ERROR;
        result.message = "Exception during test execution: " + std::string(e.what());
    }
    
    return result;
}

ValidationResult LayoutValidator::validateTestCase(
    const TestCase& testCase,
    const std::map<std::string, ElementData>& actualElements
) {
    ValidationResult overallResult;
    overallResult.selector = testCase.name;
    overallResult.status = ValidationResult::Status::PASS;
    
    int elementsPassed = 0;
    int elementsTotal = 0;
    
    // Validate each element in the reference
    for (const auto& [selector, referenceElement] : testCase.referenceElements) {
        auto actualIt = actualElements.find(selector);
        if (actualIt == actualElements.end()) {
            // Element missing in actual results
            overallResult.status = ValidationResult::Status::FAIL;
            ValidationResult::PropertyComparison missing;
            missing.property = "element_exists";
            missing.expected = 1.0;
            missing.actual = 0.0;
            missing.withinTolerance = false;
            overallResult.propertyComparisons.push_back(missing);
            continue;
        }
        
        ValidationResult elementResult = validateElement(selector, referenceElement, actualIt->second);
        
        // Merge results
        overallResult.propertyComparisons.insert(
            overallResult.propertyComparisons.end(),
            elementResult.propertyComparisons.begin(),
            elementResult.propertyComparisons.end()
        );
        
        overallResult.totalProperties += elementResult.totalProperties;
        overallResult.passedProperties += elementResult.passedProperties;
        overallResult.failedProperties += elementResult.failedProperties;
        
        elementsTotal++;
        if (elementResult.isPass()) {
            elementsPassed++;
        } else if (elementResult.isFail()) {
            overallResult.status = ValidationResult::Status::FAIL;
        }
    }
    
    // Generate summary message
    std::ostringstream oss;
    oss << elementsPassed << "/" << elementsTotal << " elements passed, "
        << overallResult.passedProperties << "/" << overallResult.totalProperties << " properties passed";
    overallResult.message = oss.str();
    
    return overallResult;
}

void TestRunner::printSummary(const TestSuiteResults& results) {
    std::cout << "\n=== Test Results Summary ===" << std::endl;
    std::cout << "Category: " << results.category << std::endl;
    std::cout << "Total Tests: " << results.totalTests << std::endl;
    std::cout << "Passed: " << results.passedTests << " (" 
              << std::fixed << std::setprecision(1) 
              << (100.0 * results.passedTests / results.totalTests) << "%)" << std::endl;
    std::cout << "Failed: " << results.failedTests << std::endl;
    std::cout << "Skipped: " << results.skippedTests << std::endl;
    std::cout << "Errors: " << results.errorTests << std::endl;
    std::cout << "Duration: " << formatDuration(results.totalTime) << std::endl;
    std::cout << std::endl;
}

void TestRunner::generateJsonReport(const TestSuiteResults& results, const std::string& outputFile) {
    // Simple JSON report generation
    std::ofstream file(outputFile);
    if (!file.is_open()) {
        std::cerr << "Failed to open output file: " << outputFile << std::endl;
        return;
    }
    
    file << "{\n";
    file << "  \"summary\": {\n";
    file << "    \"total\": " << results.totalTests << ",\n";
    file << "    \"passed\": " << results.passedTests << ",\n";
    file << "    \"failed\": " << results.failedTests << ",\n";
    file << "    \"skipped\": " << results.skippedTests << ",\n";
    file << "    \"errors\": " << results.errorTests << ",\n";
    file << "    \"time\": " << results.totalTime << "\n";
    file << "  },\n";
    file << "  \"category\": \"" << results.category << "\",\n";
    file << "  \"results\": [\n";
    
    for (size_t i = 0; i < results.results.size(); ++i) {
        const auto& result = results.results[i];
        file << "    {\n";
        file << "      \"selector\": \"" << result.selector << "\",\n";
        file << "      \"status\": \"" << (result.isPass() ? "pass" : (result.isFail() ? "fail" : (result.isSkip() ? "skip" : "error"))) << "\",\n";
        file << "      \"message\": \"" << result.message << "\"\n";
        file << "    }";
        if (i < results.results.size() - 1) file << ",";
        file << "\n";
    }
    
    file << "  ]\n";
    file << "}\n";
    file.close();
}

std::string TestRunner::formatDuration(double seconds) const {
    if (seconds < 1.0) {
        return std::to_string(static_cast<int>(seconds * 1000)) + "ms";
    } else {
        return std::to_string(static_cast<int>(seconds)) + "s";
    }
}

std::string TestRunner::getCurrentTimestamp() const {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::ostringstream oss;
    oss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

void TestRunner::ensureDirectoryExists(const std::string& dir) {
    std::filesystem::create_directories(dir);
}

// Utility Functions Implementation
namespace Utils {

std::string readFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return "";
    }
    
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

bool writeFile(const std::string& path, const std::string& content) {
    std::ofstream file(path);
    if (!file.is_open()) {
        return false;
    }
    
    file << content;
    return file.good();
}

bool fileExists(const std::string& path) {
    return std::filesystem::exists(path);
}

std::vector<std::string> listFiles(const std::string& directory, const std::string& extension) {
    std::vector<std::string> files;
    
    if (!std::filesystem::exists(directory)) {
        return files;
    }
    
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (entry.is_regular_file()) {
            std::string filename = entry.path().string();
            if (extension.empty() || (filename.length() >= extension.length() && 
                filename.substr(filename.length() - extension.length()) == extension)) {
                files.push_back(filename);
            }
        }
    }
    
    std::sort(files.begin(), files.end());
    return files;
}

std::vector<std::string> split(const std::string& str, char delimiter) {
    std::vector<std::string> tokens;
    std::stringstream ss(str);
    std::string token;
    
    while (std::getline(ss, token, delimiter)) {
        tokens.push_back(token);
    }
    
    return tokens;
}

std::string trim(const std::string& str) {
    size_t start = str.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return "";
    
    size_t end = str.find_last_not_of(" \t\n\r");
    return str.substr(start, end - start + 1);
}

bool isNearlyEqual(double a, double b, double epsilon) {
    return std::abs(a - b) < epsilon;
}

double roundToDecimalPlaces(double value, int places) {
    double multiplier = std::pow(10.0, places);
    return std::round(value * multiplier) / multiplier;
}

} // namespace Utils

// TestRunner Radiant Integration Implementation
std::map<std::string, ElementData> TestRunner::runRadiantLayout(const TestCase& testCase) {
    std::map<std::string, ElementData> results;
    
    try {
        if (verbose_) {
            std::cout << "Running Radiant layout for test: " << testCase.name << std::endl;
        }
        
        // 1. Parse HTML and CSS
        HtmlCssParser parser;
        auto rootElement = parser.parseHtml(testCase.htmlContent);
        
        if (rootElement.tag.empty()) {
            if (verbose_) {
                std::cerr << "Failed to parse HTML content" << std::endl;
            }
            return results;
        }
        
        // Extract and apply CSS rules
        auto cssRules = parser.extractCssRules(testCase.htmlContent);
        parser.applyCssToElements(rootElement, cssRules);
        
        // 2. Create layout context (following proven pattern from test_radiant_flex_gtest.cpp)
        LayoutContext* lycon = (LayoutContext*)calloc(1, sizeof(LayoutContext));
        lycon->width = 1200;  // Browser viewport width
        lycon->height = 800;  // Browser viewport height
        lycon->dpi = 96;
        
        // Initialize memory pools (critical for proper operation)
        init_view_pool(lycon);
        
        if (!lycon->pool) {
            if (verbose_) {
                std::cerr << "Failed to initialize memory pool" << std::endl;
            }
            free(lycon);
            return results;
        }
        
        // 3. Build ViewBlock tree
        ViewBlockBuilder builder(lycon);
        ViewBlock* rootBlock = builder.buildViewBlockTree(rootElement);
        
        if (!rootBlock) {
            if (verbose_) {
                std::cerr << "Failed to build ViewBlock tree" << std::endl;
            }
            cleanup_view_pool(lycon);
            free(lycon);
            return results;
        }
        
        // 4. Run layout algorithm (core integration point)
        if (rootBlock->embed && rootBlock->embed->flex_container) {
            if (verbose_) {
                std::cout << "Running flex layout algorithm..." << std::endl;
            }
            layout_flex_container_new(lycon, rootBlock);
        } else {
            if (verbose_) {
                std::cout << "Running block layout algorithm..." << std::endl;
            }
            // For non-flex containers, we could run block layout here
            // layout_block(lycon, rootBlock);
        }
        
        // 5. Extract results
        results = extractLayoutResults(rootBlock, rootElement);
        
        if (verbose_) {
            std::cout << "Extracted " << results.size() << " elements from layout" << std::endl;
        }
        
        // 6. Cleanup (critical to prevent memory leaks)
        cleanup_view_pool(lycon);
        free(lycon);
        
    } catch (const std::exception& e) {
        if (verbose_) {
            std::cerr << "Layout execution error: " << e.what() << std::endl;
        }
        // Return empty results on error
        results.clear();
    }
    
    return results;
}

std::map<std::string, ElementData> TestRunner::extractLayoutResults(
    ViewBlock* rootBlock, 
    const HtmlCssParser::ParsedElement& rootElement) {
    
    std::map<std::string, ElementData> results;
    
    if (!rootBlock) return results;
    
    // Traverse ViewBlock tree and extract layout data
    extractElementData(rootBlock, rootElement, results, "");
    
    return results;
}

void TestRunner::extractElementData(
    ViewBlock* block, 
    const HtmlCssParser::ParsedElement& element,
    std::map<std::string, ElementData>& results,
    const std::string& selectorPrefix) {
    
    if (!block) return;
    
    // Create selector (e.g., ".container", ".item:nth-child(1)")
    std::string selector = buildSelector(element, selectorPrefix);
    
    // Extract layout properties
    ElementData data;
    data.selector = selector;
    data.tag = element.tag;
    data.id = element.id;
    data.classes = element.classes;
    
    // Core layout data extraction
    data.layout.x = static_cast<double>(block->x);
    data.layout.y = static_cast<double>(block->y);
    data.layout.width = static_cast<double>(block->width);
    data.layout.height = static_cast<double>(block->height);
    data.layout.contentWidth = static_cast<double>(block->content_width);
    data.layout.contentHeight = static_cast<double>(block->content_height);
    
    // Flex-specific properties
    if (block->embed && block->embed->flex_container) {
        data.layout.display = "flex";
        data.layout.flexDirection = mapRadiantToCSS(block->embed->flex_container->direction);
        data.layout.justifyContent = mapRadiantToCSS(block->embed->flex_container->justify);
        data.layout.alignItems = mapRadiantToCSS(block->embed->flex_container->align_items);
        data.layout.alignContent = mapRadiantToCSS(block->embed->flex_container->align_content);
        
        // Gap properties
        if (block->embed->flex_container->column_gap > 0 || block->embed->flex_container->row_gap > 0) {
            if (block->embed->flex_container->column_gap == block->embed->flex_container->row_gap) {
                // Single gap value
                data.layout.gap = block->embed->flex_container->column_gap;
            } else {
                // Different row/column gaps - use column gap for now
                data.layout.gap = block->embed->flex_container->column_gap;
            }
        }
    } else {
        data.layout.display = "block";
    }
    
    // Flex item properties
    if (block->flex_grow > 0 || block->flex_shrink != 1.0f || block->flex_basis != -1) {
        data.layout.flexGrow = static_cast<double>(block->flex_grow);
        data.layout.flexShrink = static_cast<double>(block->flex_shrink);
        data.layout.flexBasis = block->flex_basis == -1 ? "auto" : std::to_string(block->flex_basis) + "px";
    }
    
    // Order property
    if (block->order != 0) {
        data.layout.order = block->order;
    }
    
    results[selector] = data;
    
    // Recursively process children
    ViewBlock* child = block->first_child;
    size_t childIndex = 0;
    while (child && childIndex < element.children.size()) {
        extractElementData(child, element.children[childIndex], results, selector);
        child = child->next_sibling;
        childIndex++;
    }
}

std::string TestRunner::buildSelector(const HtmlCssParser::ParsedElement& element, const std::string& prefix) {
    std::string selector;
    
    // Build selector based on element properties
    if (!element.id.empty()) {
        selector = "#" + element.id;
    } else if (!element.classes.empty()) {
        selector = "." + element.classes[0];
    } else {
        selector = element.tag;
    }
    
    // Add prefix if provided
    if (!prefix.empty()) {
        selector = prefix + " " + selector;
    }
    
    return selector;
}

std::string TestRunner::mapRadiantToCSS(int radiantValue) {
    // Map Radiant/Lexbor constants back to CSS values
    switch (radiantValue) {
        // Flex direction
        case LXB_CSS_VALUE_ROW: return "row";
        case LXB_CSS_VALUE_ROW_REVERSE: return "row-reverse";
        case LXB_CSS_VALUE_COLUMN: return "column";
        case LXB_CSS_VALUE_COLUMN_REVERSE: return "column-reverse";
        
        // Justify content
        case LXB_CSS_VALUE_FLEX_START: return "flex-start";
        case LXB_CSS_VALUE_FLEX_END: return "flex-end";
        case LXB_CSS_VALUE_CENTER: return "center";
        case LXB_CSS_VALUE_SPACE_BETWEEN: return "space-between";
        case LXB_CSS_VALUE_SPACE_AROUND: return "space-around";
        case LXB_CSS_VALUE_SPACE_EVENLY: return "space-evenly";
        
        // Align items/content
        case LXB_CSS_VALUE_STRETCH: return "stretch";
        case LXB_CSS_VALUE_BASELINE: return "baseline";
        
        // Wrap
        case LXB_CSS_VALUE_NOWRAP: return "nowrap";
        case LXB_CSS_VALUE_WRAP: return "wrap";
        case LXB_CSS_VALUE_WRAP_REVERSE: return "wrap-reverse";
        
        // Auto
        case LXB_CSS_VALUE_AUTO: return "auto";
        
        default: return "flex-start"; // Default fallback
    }
}

} // namespace LayoutTest
