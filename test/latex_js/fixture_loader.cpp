#include "fixture_loader.h"
#include <fstream>
#include <sstream>
#include <regex>
#include <filesystem>
#include <iostream>

FixtureLoader::FixtureLoader() {}

FixtureLoader::~FixtureLoader() {}

std::string FixtureLoader::read_file(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open fixture file: " + filepath);
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

std::string FixtureLoader::trim(const std::string& str) {
    size_t start = str.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    
    size_t end = str.find_last_not_of(" \t\r\n");
    return str.substr(start, end - start + 1);
}

bool FixtureLoader::starts_with(const std::string& str, const std::string& prefix) {
    return str.length() >= prefix.length() && 
           str.compare(0, prefix.length(), prefix) == 0;
}

std::vector<std::string> FixtureLoader::split_by_separator(const std::string& content, const std::string& separator) {
    std::vector<std::string> parts;
    
    // Simple approach: split by lines that contain only the separator
    std::istringstream stream(content);
    std::string line;
    std::string current_part;
    
    while (std::getline(stream, line)) {
        // Check if line is just the separator (with optional whitespace)
        std::string trimmed_line = trim(line);
        if (trimmed_line == separator) {
            // Found separator, save current part and start new one
            parts.push_back(current_part);
            current_part.clear();
        } else {
            // Add line to current part
            if (!current_part.empty()) {
                current_part += "\n";
            }
            current_part += line;
        }
    }
    
    // Add the last part if not empty
    if (!current_part.empty()) {
        parts.push_back(current_part);
    }
    
    // Debug output removed for cleaner test runs
    
    return parts;
}

std::vector<LatexHtmlFixture> FixtureLoader::parse_fixtures(const std::string& content, const std::string& filename) {
    std::vector<LatexHtmlFixture> fixtures;
    
    // Split content by separator "."
    std::vector<std::string> parts = split_by_separator(content, ".");
    
    int fixture_id = 1;
    
    // Process fixtures in groups of 3: header, source, expected
    for (size_t i = 0; i < parts.size(); i += 3) {
        if (i + 2 >= parts.size()) break;
        
        LatexHtmlFixture fixture;
        fixture.id = fixture_id++;
        fixture.filename = filename;
        fixture.skip_test = false;
        fixture.screenshot_test = false;
        
        // Parse header with special prefixes
        std::string header = trim(parts[i]);
        
        // Handle special prefixes from LaTeX.js
        if (starts_with(header, "!")) {
            fixture.skip_test = true;
            header = header.substr(1);
        }
        if (starts_with(header, "s")) {
            fixture.screenshot_test = true;
            header = header.substr(1);
        }
        if (starts_with(header, "** ")) {
            header = header.substr(3);
        }
        
        fixture.header = trim(header);
        fixture.latex_source = parts[i + 1];
        fixture.expected_html = parts[i + 2];
        
        // Skip empty fixtures
        if (fixture.latex_source.empty() || fixture.expected_html.empty()) {
            continue;
        }
        
        fixtures.push_back(fixture);
    }
    
    return fixtures;
}

FixtureFile FixtureLoader::load_fixture_file(const std::string& filepath) {
    FixtureFile fixture_file;
    fixture_file.filepath = filepath;
    
    try {
        std::string content = read_file(filepath);
        fixture_file.fixtures = parse_fixtures(content, std::filesystem::path(filepath).filename().string());
    } catch (const std::exception& e) {
        std::cerr << "Error loading fixture file " << filepath << ": " << e.what() << std::endl;
    }
    
    return fixture_file;
}

std::vector<FixtureFile> FixtureLoader::load_fixtures_directory(const std::string& directory_path) {
    std::vector<FixtureFile> fixture_files;
    
    // std::cout << "DEBUG: Loading fixtures from directory: " << directory_path << std::endl;
    
    try {
        for (const auto& entry : std::filesystem::directory_iterator(directory_path)) {
            if (entry.is_regular_file() && entry.path().extension() == ".tex") {
                FixtureFile fixture_file = load_fixture_file(entry.path().string());
                if (!fixture_file.fixtures.empty()) {
                    fixture_files.push_back(fixture_file);
                }
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error loading fixtures directory " << directory_path << ": " << e.what() << std::endl;
    }
    
    // std::cout << "DEBUG: Total fixture files loaded: " << fixture_files.size() << std::endl;
    return fixture_files;
}
