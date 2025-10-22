#ifndef FIXTURE_LOADER_H
#define FIXTURE_LOADER_H

#include "../../lib/stringbuf.h"
#include "../../lib/mempool.h"
#include <vector>
#include <string>

struct LatexHtmlFixture {
    int id;
    std::string header;
    std::string latex_source;
    std::string expected_html;
    std::string filename;
    bool skip_test;
    bool screenshot_test;
};

struct FixtureFile {
    std::string filepath;
    std::vector<LatexHtmlFixture> fixtures;
};

class FixtureLoader {
public:
    FixtureLoader();
    ~FixtureLoader();
    
    // Load fixtures from a single file
    FixtureFile load_fixture_file(const std::string& filepath);
    
    // Load all fixtures from a directory
    std::vector<FixtureFile> load_fixtures_directory(const std::string& directory_path);
    
    // Parse fixture content from string
    std::vector<LatexHtmlFixture> parse_fixtures(const std::string& content, const std::string& filename);
    
private:
    std::string read_file(const std::string& filepath);
    std::vector<std::string> split_by_separator(const std::string& content, const std::string& separator);
    std::string trim(const std::string& str);
    bool starts_with(const std::string& str, const std::string& prefix);
};

#endif // FIXTURE_LOADER_H
