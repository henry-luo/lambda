#include <cstdint>
#include <cstdlib>
#include <string>
#include <fstream>
#include <iostream>

// Forward declare the parse function
extern "C" bool parse_lambda_file(const char* filename, void* doc);

// Fuzzer entry point
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    // Convert input to string
    std::string input(reinterpret_cast<const char*>(data), size);
    
    // Create a temporary file with the input
    std::string temp_file = "/tmp/fuzz_input.ls";
    {
        std::ofstream f(temp_file, std::ios::binary);
        if (!f) return 0;
        f.write(reinterpret_cast<const char*>(data), size);
    }
    
    // Simple document structure to pass to the parser
    struct SimpleDoc {
        // Add minimal structure needed for parsing
        bool valid = true;
    } doc;
    
    // Parse the input
    bool success = parse_lambda_file(temp_file.c_str(), &doc);
    
    // Clean up
    remove(temp_file.c_str());
    
    return success ? 0 : -1;
}
