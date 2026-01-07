#include "math_fixture_loader.h"
#include "../../lib/log.h"

#include <fstream>
#include <sstream>
#include <dirent.h>
#include <sys/stat.h>
#include <cstring>

// Simple JSON parsing helpers (avoiding external dependencies)
// For production, consider using a proper JSON library

static std::string trim_whitespace(const std::string& str) {
    size_t start = str.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return "";
    size_t end = str.find_last_not_of(" \t\n\r");
    return str.substr(start, end - start + 1);
}

static std::string unescape_json_string(const std::string& str) {
    std::string result;
    result.reserve(str.size());

    for (size_t i = 0; i < str.size(); i++) {
        if (str[i] == '\\' && i + 1 < str.size()) {
            char next = str[i + 1];
            switch (next) {
                case 'n': result += '\n'; i++; break;
                case 't': result += '\t'; i++; break;
                case 'r': result += '\r'; i++; break;
                case '"': result += '"'; i++; break;
                case '\\': result += '\\'; i++; break;
                default: result += str[i]; break;
            }
        } else {
            result += str[i];
        }
    }

    return result;
}

static std::string extract_string_value(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return "";

    // Find the colon after the key
    pos = json.find(':', pos + search.length());
    if (pos == std::string::npos) return "";

    // Skip whitespace
    pos++;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n')) {
        pos++;
    }

    if (pos >= json.size() || json[pos] != '"') return "";

    // Find the closing quote (handling escapes)
    size_t start = pos + 1;
    size_t end = start;
    while (end < json.size()) {
        if (json[end] == '\\' && end + 1 < json.size()) {
            end += 2;  // skip escaped character
        } else if (json[end] == '"') {
            break;
        } else {
            end++;
        }
    }

    return unescape_json_string(json.substr(start, end - start));
}

static int extract_int_value(const std::string& json, const std::string& key, int default_val = 0) {
    std::string search = "\"" + key + "\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return default_val;

    pos = json.find(':', pos + search.length());
    if (pos == std::string::npos) return default_val;

    pos++;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n')) {
        pos++;
    }

    // Parse integer
    std::string num_str;
    while (pos < json.size() && (isdigit(json[pos]) || json[pos] == '-')) {
        num_str += json[pos++];
    }

    if (num_str.empty()) return default_val;
    return std::stoi(num_str);
}

static std::vector<std::string> extract_array_objects(const std::string& json, const std::string& key) {
    std::vector<std::string> result;

    std::string search = "\"" + key + "\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return result;

    pos = json.find('[', pos);
    if (pos == std::string::npos) return result;

    // Find matching ]
    int depth = 1;
    size_t start = pos + 1;
    size_t obj_start = std::string::npos;
    int obj_depth = 0;

    for (size_t i = start; i < json.size() && depth > 0; i++) {
        char c = json[i];

        if (c == '[') {
            depth++;
        } else if (c == ']') {
            depth--;
        } else if (c == '{') {
            if (obj_depth == 0) {
                obj_start = i;
            }
            obj_depth++;
        } else if (c == '}') {
            obj_depth--;
            if (obj_depth == 0 && obj_start != std::string::npos) {
                result.push_back(json.substr(obj_start, i - obj_start + 1));
                obj_start = std::string::npos;
            }
        } else if (c == '"') {
            // Skip string content
            i++;
            while (i < json.size() && json[i] != '"') {
                if (json[i] == '\\') i++;
                i++;
            }
        }
    }

    return result;
}

// ============================================================================
// MathFixtureLoader implementation
// ============================================================================

MathFixtureLoader::MathFixtureLoader() {
}

MathFixtureLoader::~MathFixtureLoader() {
}

std::string MathFixtureLoader::read_file(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        log_error("math_fixture_loader: failed to open file: %s", filepath.c_str());
        return "";
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

MathTestCase MathFixtureLoader::parse_test_case(const std::string& json_str, const std::string& category) {
    MathTestCase test;

    test.id = extract_int_value(json_str, "id", 0);
    test.latex = extract_string_value(json_str, "latex");
    test.description = extract_string_value(json_str, "description");
    test.source = extract_string_value(json_str, "source");
    test.reference_image = extract_string_value(json_str, "reference_image");
    test.category = category;

    // TODO: Parse expected dimensions if present
    // For now, we don't have expected values in the fixtures

    return test;
}

MathTestCategory MathFixtureLoader::load_fixture_file(const std::string& filepath) {
    MathTestCategory category;

    std::string content = read_file(filepath);
    if (content.empty()) {
        return category;
    }

    category.name = extract_string_value(content, "category");
    category.source = extract_string_value(content, "source");

    // Parse tests array
    std::vector<std::string> test_objects = extract_array_objects(content, "tests");

    for (const auto& test_json : test_objects) {
        MathTestCase test = parse_test_case(test_json, category.name);
        if (!test.latex.empty()) {
            category.tests.push_back(test);
        }
    }

    log_debug("math_fixture_loader: loaded %zu tests from %s",
              category.tests.size(), filepath.c_str());

    return category;
}

std::vector<MathTestCategory> MathFixtureLoader::load_fixtures_directory(const std::string& directory_path) {
    std::vector<MathTestCategory> categories;

    DIR* dir = opendir(directory_path.c_str());
    if (!dir) {
        log_error("math_fixture_loader: failed to open directory: %s", directory_path.c_str());
        return categories;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string filename = entry->d_name;

        // Skip non-JSON files and the combined file
        if (filename.size() < 5 ||
            filename.substr(filename.size() - 5) != ".json" ||
            filename == "all_tests.json") {
            continue;
        }

        std::string filepath = directory_path + "/" + filename;

        // Check if it's a regular file
        struct stat st;
        if (stat(filepath.c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
            MathTestCategory cat = load_fixture_file(filepath);
            if (!cat.tests.empty()) {
                categories.push_back(cat);
            }
        }
    }

    closedir(dir);

    log_info("math_fixture_loader: loaded %zu categories from %s",
             categories.size(), directory_path.c_str());

    return categories;
}

std::vector<MathTestCategory> MathFixtureLoader::load_combined_fixtures(const std::string& filepath) {
    std::vector<MathTestCategory> categories;

    std::string content = read_file(filepath);
    if (content.empty()) {
        return categories;
    }

    // Find "categories" object
    size_t cat_pos = content.find("\"categories\"");
    if (cat_pos == std::string::npos) {
        log_error("math_fixture_loader: no 'categories' key in %s", filepath.c_str());
        return categories;
    }

    // Find the opening brace of categories object
    size_t brace_pos = content.find('{', cat_pos);
    if (brace_pos == std::string::npos) {
        return categories;
    }

    // Extract category names and their test arrays
    // This is a simplified parser - for complex JSON, use a proper library

    // Pattern: "category_name": [ ... ]
    size_t pos = brace_pos + 1;
    while (pos < content.size()) {
        // Find next category name
        size_t quote_start = content.find('"', pos);
        if (quote_start == std::string::npos || quote_start > content.size() - 10) break;

        size_t quote_end = content.find('"', quote_start + 1);
        if (quote_end == std::string::npos) break;

        std::string cat_name = content.substr(quote_start + 1, quote_end - quote_start - 1);

        // Find the array for this category
        size_t colon_pos = content.find(':', quote_end);
        if (colon_pos == std::string::npos) break;

        size_t array_start = content.find('[', colon_pos);
        if (array_start == std::string::npos) break;

        // Find matching ]
        int depth = 1;
        size_t array_end = array_start + 1;
        while (array_end < content.size() && depth > 0) {
            if (content[array_end] == '[') depth++;
            else if (content[array_end] == ']') depth--;
            else if (content[array_end] == '"') {
                // Skip string
                array_end++;
                while (array_end < content.size() && content[array_end] != '"') {
                    if (content[array_end] == '\\') array_end++;
                    array_end++;
                }
            }
            array_end++;
        }

        // Extract test objects from the array
        std::string array_content = content.substr(array_start, array_end - array_start);

        MathTestCategory category;
        category.name = cat_name;
        category.source = "mathlive";

        // Parse test objects
        std::vector<std::string> test_objects;
        int obj_depth = 0;
        size_t obj_start = std::string::npos;

        for (size_t i = 0; i < array_content.size(); i++) {
            if (array_content[i] == '{') {
                if (obj_depth == 0) obj_start = i;
                obj_depth++;
            } else if (array_content[i] == '}') {
                obj_depth--;
                if (obj_depth == 0 && obj_start != std::string::npos) {
                    test_objects.push_back(array_content.substr(obj_start, i - obj_start + 1));
                    obj_start = std::string::npos;
                }
            } else if (array_content[i] == '"') {
                i++;
                while (i < array_content.size() && array_content[i] != '"') {
                    if (array_content[i] == '\\') i++;
                    i++;
                }
            }
        }

        for (const auto& test_json : test_objects) {
            MathTestCase test = parse_test_case(test_json, cat_name);
            if (!test.latex.empty()) {
                category.tests.push_back(test);
            }
        }

        if (!category.tests.empty()) {
            categories.push_back(category);
        }

        pos = array_end;
    }

    log_info("math_fixture_loader: loaded %zu categories from combined file", categories.size());

    return categories;
}

std::vector<MathTestCase> MathFixtureLoader::get_all_tests(const std::vector<MathTestCategory>& categories) {
    std::vector<MathTestCase> all_tests;

    for (const auto& cat : categories) {
        for (const auto& test : cat.tests) {
            all_tests.push_back(test);
        }
    }

    return all_tests;
}

std::vector<MathTestCase> MathFixtureLoader::filter_by_category(
    const std::vector<MathTestCategory>& categories,
    const std::string& category_name
) {
    std::vector<MathTestCase> filtered;

    for (const auto& cat : categories) {
        if (cat.name == category_name) {
            for (const auto& test : cat.tests) {
                filtered.push_back(test);
            }
        }
    }

    return filtered;
}
