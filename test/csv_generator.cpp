#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <sstream>
#include <filesystem>

// Simple JSON parser for test results
struct TestResult {
    std::string name;
    std::string category;
    std::string file_path;
    std::string expected;
    std::string actual;
    bool passed;
    std::string test_expression;
};

// Escape CSV special characters
std::string escapeCSV(const std::string& str) {
    std::string escaped = str;
    bool needsQuotes = false;
    
    // Check if we need to quote the field
    if (escaped.find(',') != std::string::npos ||
        escaped.find('"') != std::string::npos ||
        escaped.find('\n') != std::string::npos ||
        escaped.find('\r') != std::string::npos) {
        needsQuotes = true;
    }
    
    // Escape quotes by doubling them
    size_t pos = 0;
    while ((pos = escaped.find('"', pos)) != std::string::npos) {
        escaped.replace(pos, 1, "\"\"");
        pos += 2;
    }
    
    // Add quotes if needed
    if (needsQuotes) {
        escaped = "\"" + escaped + "\"";
    }
    
    return escaped;
}

// Extract test expression from Lambda file
std::string extractTestExpression(const std::string& file_path) {
    std::ifstream file(file_path);
    if (!file.is_open()) {
        return "";
    }
    
    std::string line;
    std::string expression;
    
    while (std::getline(file, line)) {
        if (!expression.empty()) {
            expression += "\n";
        }
        expression += line;
    }
    
    return expression;
}

// Simple JSON value extraction
std::string extractJsonString(const std::string& json, const std::string& key) {
    std::string searchKey = "\"" + key + "\":";
    size_t pos = json.find(searchKey);
    if (pos == std::string::npos) return "";
    
    pos += searchKey.length();
    
    // Skip whitespace
    while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
    
    if (pos >= json.length() || json[pos] != '"') return "";
    pos++; // Skip opening quote
    
    std::string result;
    while (pos < json.length() && json[pos] != '"') {
        if (json[pos] == '\\' && pos + 1 < json.length()) {
            pos++; // Skip escape character
            switch (json[pos]) {
                case 'n': result += '\n'; break;
                case 'r': result += '\r'; break;
                case 't': result += '\t'; break;
                case '\\': result += '\\'; break;
                case '"': result += '"'; break;
                default: result += json[pos]; break;
            }
        } else {
            result += json[pos];
        }
        pos++;
    }
    
    return result;
}

bool extractJsonBool(const std::string& json, const std::string& key) {
    std::string searchKey = "\"" + key + "\":";
    size_t pos = json.find(searchKey);
    if (pos == std::string::npos) return false;
    
    pos += searchKey.length();
    
    // Skip whitespace
    while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
    
    return (pos < json.length() - 4 && json.substr(pos, 4) == "true");
}

int main(int argc, char* argv[]) {
    std::string json_file = "test_output/lambda_test_runner_results.json";
    std::string csv_file = "test_output/test_results.csv";
    
    if (argc > 1) {
        json_file = argv[1];
    }
    if (argc > 2) {
        csv_file = argv[2];
    }
    
    // Read JSON file
    std::ifstream json_input(json_file);
    if (!json_input.is_open()) {
        std::cerr << "Error: Could not open JSON file: " << json_file << std::endl;
        return 1;
    }
    
    std::string json_content((std::istreambuf_iterator<char>(json_input)),
                             std::istreambuf_iterator<char>());
    json_input.close();
    
    // Create CSV file
    std::ofstream csv_output(csv_file);
    if (!csv_output.is_open()) {
        std::cerr << "Error: Could not create CSV file: " << csv_file << std::endl;
        return 1;
    }
    
    // Write CSV header
    csv_output << "Test Suite Name,Test Name,Test Expression,Expected Output,Actual Output,Pass or Fail\n";
    
    // Parse tests from JSON
    size_t tests_pos = json_content.find("\"tests\":");
    if (tests_pos == std::string::npos) {
        std::cerr << "Error: Could not find tests array in JSON" << std::endl;
        return 1;
    }
    
    // Find the start of the tests array
    size_t array_start = json_content.find('[', tests_pos);
    if (array_start == std::string::npos) {
        std::cerr << "Error: Could not find tests array start" << std::endl;
        return 1;
    }
    
    // Parse each test object
    size_t pos = array_start + 1;
    while (pos < json_content.length()) {
        // Skip whitespace
        while (pos < json_content.length() && (json_content[pos] == ' ' || json_content[pos] == '\n' || json_content[pos] == '\t')) pos++;
        
        if (pos >= json_content.length() || json_content[pos] == ']') break;
        
        if (json_content[pos] == '{') {
            // Find the end of this test object
            int brace_count = 1;
            size_t test_start = pos;
            pos++;
            
            while (pos < json_content.length() && brace_count > 0) {
                if (json_content[pos] == '{') brace_count++;
                else if (json_content[pos] == '}') brace_count--;
                pos++;
            }
            
            std::string test_json = json_content.substr(test_start, pos - test_start);
            
            // Extract test data
            TestResult test;
            test.name = extractJsonString(test_json, "name");
            test.category = extractJsonString(test_json, "category");
            test.file_path = extractJsonString(test_json, "file");
            test.expected = extractJsonString(test_json, "expected");
            test.actual = extractJsonString(test_json, "actual");
            test.passed = extractJsonBool(test_json, "passed");
            
            // Extract test expression from file
            test.test_expression = extractTestExpression(test.file_path);
            
            // Write CSV row
            csv_output << escapeCSV(test.category) << ","
                      << escapeCSV(test.name) << ","
                      << escapeCSV(test.test_expression) << ","
                      << escapeCSV(test.expected) << ","
                      << escapeCSV(test.actual) << ","
                      << (test.passed ? "PASS" : "FAIL") << "\n";
        }
        
        // Skip to next test or end
        while (pos < json_content.length() && json_content[pos] != '{' && json_content[pos] != ']') pos++;
    }
    
    csv_output.close();
    std::cout << "CSV report generated: " << csv_file << std::endl;
    
    return 0;
}
