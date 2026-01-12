#ifndef MATH_FIXTURE_LOADER_H
#define MATH_FIXTURE_LOADER_H

#include <vector>
#include <string>

/**
 * A single math layout test case.
 */
struct MathTestCase {
    int id;
    int index;  // 0-based index within category for test naming
    std::string latex;
    std::string description;
    std::string source;
    std::string reference_image;  // optional reference image path
    std::string category;

    // Expected dimensions (optional, in em units relative to font size)
    bool has_expected_height;
    float expected_height_min;
    float expected_height_max;

    bool has_expected_depth;
    float expected_depth_min;
    float expected_depth_max;

    bool has_expected_width;
    float expected_width_min;
    float expected_width_max;

    MathTestCase()
        : id(0)
        , index(0)
        , has_expected_height(false)
        , expected_height_min(0), expected_height_max(0)
        , has_expected_depth(false)
        , expected_depth_min(0), expected_depth_max(0)
        , has_expected_width(false)
        , expected_width_min(0), expected_width_max(0)
    {}
};

/**
 * A category of math tests.
 */
struct MathTestCategory {
    std::string name;
    std::string source;
    std::vector<MathTestCase> tests;
};

/**
 * Load math test fixtures from JSON files.
 */
class MathFixtureLoader {
public:
    MathFixtureLoader();
    ~MathFixtureLoader();

    /**
     * Load a single fixture file.
     * @param filepath Path to JSON fixture file
     * @return Category with test cases
     */
    MathTestCategory load_fixture_file(const std::string& filepath);

    /**
     * Load all fixture files from a directory.
     * @param directory_path Path to fixtures directory
     * @return Vector of categories
     */
    std::vector<MathTestCategory> load_fixtures_directory(const std::string& directory_path);

    /**
     * Load the combined all_tests.json file.
     * @param filepath Path to all_tests.json
     * @return Vector of all categories
     */
    std::vector<MathTestCategory> load_combined_fixtures(const std::string& filepath);

    /**
     * Get all test cases flattened into a single vector.
     * Useful for parameterized tests.
     */
    std::vector<MathTestCase> get_all_tests(const std::vector<MathTestCategory>& categories);

    /**
     * Filter tests by category name.
     */
    std::vector<MathTestCase> filter_by_category(
        const std::vector<MathTestCategory>& categories,
        const std::string& category_name
    );

private:
    std::string read_file(const std::string& filepath);
    MathTestCase parse_test_case(const std::string& json_str, const std::string& category);
};

#endif // MATH_FIXTURE_LOADER_H
