// Clang-based refactoring tool to convert printf/fprintf to log_debug
// Compile: clang++ -std=c++17 refactor_to_log_debug.cpp -lclang -o refactor_to_log_debug
// Usage: ./refactor_to_log_debug <source_file.c> [--dry-run] [--backup]

#include <clang-c/Index.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <algorithm>
#include <cstring>
#include <set>

struct Replacement {
    unsigned offset;
    unsigned length;
    std::string text;
    
    bool operator<(const Replacement& other) const {
        return offset < other.offset;
    }
};

class RefactoringContext {
public:
    std::vector<Replacement> replacements;
    std::set<std::string> includes_needed;
    CXTranslationUnit tu;
    std::string source_code;
    int changes_count = 0;
    
    void addReplacement(CXCursor cursor, const std::string& new_text) {
        CXSourceRange range = clang_getCursorExtent(cursor);
        CXSourceLocation start = clang_getRangeStart(range);
        CXSourceLocation end = clang_getRangeEnd(range);
        
        unsigned start_offset, end_offset;
        clang_getSpellingLocation(start, nullptr, nullptr, nullptr, &start_offset);
        clang_getSpellingLocation(end, nullptr, nullptr, nullptr, &end_offset);
        
        Replacement repl;
        repl.offset = start_offset;
        repl.length = end_offset - start_offset;
        repl.text = new_text;
        
        replacements.push_back(repl);
        changes_count++;
    }
    
    void addInclude(const std::string& include) {
        includes_needed.insert(include);
    }
};

std::string getCursorText(CXCursor cursor, const std::string& source_code) {
    CXSourceRange range = clang_getCursorExtent(cursor);
    CXSourceLocation start = clang_getRangeStart(range);
    CXSourceLocation end = clang_getRangeEnd(range);
    
    unsigned start_offset, end_offset;
    clang_getSpellingLocation(start, nullptr, nullptr, nullptr, &start_offset);
    clang_getSpellingLocation(end, nullptr, nullptr, nullptr, &end_offset);
    
    if (end_offset > start_offset && end_offset <= source_code.length()) {
        return source_code.substr(start_offset, end_offset - start_offset);
    }
    return "";
}

std::string getFunctionName(CXCursor cursor) {
    CXString name = clang_getCursorSpelling(cursor);
    std::string result = clang_getCString(name);
    clang_disposeString(name);
    return result;
}

std::string extractArgumentsAfterFirst(CXCursor call_cursor, const std::string& source_code) {
    std::vector<std::string> args;
    
    int arg_count = clang_Cursor_getNumArguments(call_cursor);
    if (arg_count <= 1) {
        return "";
    }
    
    // Get all arguments after the first one (skip stderr)
    std::string result;
    for (int i = 1; i < arg_count; i++) {
        CXCursor arg = clang_Cursor_getArgument(call_cursor, i);
        std::string arg_text = getCursorText(arg, source_code);
        if (!result.empty()) {
            result += ", ";
        }
        result += arg_text;
    }
    
    return result;
}

std::string getAllArguments(CXCursor call_cursor, const std::string& source_code) {
    std::string result;
    int arg_count = clang_Cursor_getNumArguments(call_cursor);
    
    for (int i = 0; i < arg_count; i++) {
        CXCursor arg = clang_Cursor_getArgument(call_cursor, i);
        std::string arg_text = getCursorText(arg, source_code);
        if (!result.empty()) {
            result += ", ";
        }
        result += arg_text;
    }
    
    return result;
}

bool isStderrArgument(CXCursor cursor, const std::string& source_code) {
    CXCursorKind kind = clang_getCursorKind(cursor);
    
    // Get the text of the argument
    std::string arg_text = getCursorText(cursor, source_code);
    
    // Check if it's literally "stderr"
    if (arg_text == "stderr") {
        return true;
    }
    
    // Check if it's a DeclRefExpr referring to stderr
    if (kind == CXCursor_DeclRefExpr || kind == CXCursor_UnexposedExpr) {
        std::string name = getFunctionName(cursor);
        if (name == "stderr") {
            return true;
        }
    }
    
    return false;
}

CXChildVisitResult visitorFunction(CXCursor cursor, CXCursor parent, CXClientData client_data) {
    RefactoringContext* ctx = static_cast<RefactoringContext*>(client_data);
    
    CXCursorKind kind = clang_getCursorKind(cursor);
    
    // Look for function calls
    if (kind == CXCursor_CallExpr) {
        std::string func_name = getFunctionName(cursor);
        
        // Handle printf calls
        if (func_name == "printf") {
            std::string args = getAllArguments(cursor, ctx->source_code);
            std::string replacement = "log_debug(" + args + ")";
            ctx->addReplacement(cursor, replacement);
            ctx->addInclude("log.h");
            return CXChildVisit_Continue;
        }
        
        // Handle fprintf(stderr, ...) calls
        if (func_name == "fprintf") {
            int arg_count = clang_Cursor_getNumArguments(cursor);
            if (arg_count >= 1) {
                CXCursor first_arg = clang_Cursor_getArgument(cursor, 0);
                
                // Check if first argument is stderr
                if (isStderrArgument(first_arg, ctx->source_code)) {
                    std::string remaining_args = extractArgumentsAfterFirst(cursor, ctx->source_code);
                    std::string replacement = "log_debug(" + remaining_args + ")";
                    ctx->addReplacement(cursor, replacement);
                    ctx->addInclude("log.h");
                    return CXChildVisit_Continue;
                }
            }
        }
    }
    
    return CXChildVisit_Recurse;
}

std::string applyReplacements(const std::string& source, std::vector<Replacement>& replacements) {
    // Sort replacements by offset in reverse order to apply from end to start
    std::sort(replacements.begin(), replacements.end(), 
              [](const Replacement& a, const Replacement& b) { return a.offset > b.offset; });
    
    std::string result = source;
    
    for (const auto& repl : replacements) {
        if (repl.offset + repl.length <= result.length()) {
            result.replace(repl.offset, repl.length, repl.text);
        }
    }
    
    return result;
}

std::string getRelativeLogInclude(const std::string& filepath) {
    // The build system adds -Ilib to include paths, so we can always use lib/log.h
    // regardless of where the file is located
    return "lib/log.h";
}

std::string addIncludeIfNeeded(const std::string& source, const std::string& include_file, const std::string& filepath) {
    // Calculate the correct relative path for the include
    std::string include_path = getRelativeLogInclude(filepath);
    std::string include_directive = "#include \"" + include_path + "\"";
    
    // Check if any form of log.h include already exists
    if (source.find("#include \"log.h\"") != std::string::npos ||
        source.find("#include <log.h>") != std::string::npos ||
        source.find("#include \"lib/log.h\"") != std::string::npos ||
        source.find("#include <lib/log.h>") != std::string::npos) {
        return source;
    }
    
    // Find the last #include and insert after it
    size_t last_include_pos = 0;
    size_t pos = 0;
    
    while ((pos = source.find("#include", pos)) != std::string::npos) {
        size_t eol = source.find('\n', pos);
        if (eol != std::string::npos) {
            last_include_pos = eol + 1;
            pos = eol + 1;
        } else {
            break;
        }
    }
    
    std::string result = source;
    if (last_include_pos > 0) {
        result.insert(last_include_pos, include_directive + "\n");
    } else {
        // No includes found, add at the beginning after comments
        size_t insert_pos = 0;
        result.insert(insert_pos, include_directive + "\n");
    }
    
    return result;
}

bool processFile(const std::string& filepath, bool dry_run, bool backup) {
    // Read source file
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "Error: Cannot open file " << filepath << std::endl;
        return false;
    }
    
    std::string source_code((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());
    file.close();
    
    // Create clang index
    CXIndex index = clang_createIndex(0, 0);
    
    // Parse the file with more comprehensive include paths
    std::vector<const char*> args;
    args.push_back("-I/usr/include");
    args.push_back("-I/usr/local/include");
    args.push_back("-I/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/usr/include");
    args.push_back("-I/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk/usr/include");
    
    // Add current directory and common project paths
    args.push_back("-I.");
    args.push_back("-Iinclude");
    args.push_back("-Ilib");
    args.push_back("-Ilambda");
    
    CXTranslationUnit tu = clang_parseTranslationUnit(
        index,
        filepath.c_str(),
        args.data(), args.size(),
        nullptr, 0,
        CXTranslationUnit_KeepGoing
    );
    
    if (!tu) {
        std::cerr << "Error: Cannot parse translation unit" << std::endl;
        clang_disposeIndex(index);
        return false;
    }
    
    // Check for parse errors
    unsigned num_diagnostics = clang_getNumDiagnostics(tu);
    bool has_errors = false;
    for (unsigned i = 0; i < num_diagnostics; i++) {
        CXDiagnostic diag = clang_getDiagnostic(tu, i);
        CXDiagnosticSeverity severity = clang_getDiagnosticSeverity(diag);
        if (severity >= CXDiagnostic_Error) {
            has_errors = true;
            CXString str = clang_formatDiagnostic(diag, clang_defaultDiagnosticDisplayOptions());
            std::cerr << "Parse error: " << clang_getCString(str) << std::endl;
            clang_disposeString(str);
        }
        clang_disposeDiagnostic(diag);
    }
    
    if (has_errors) {
        std::cerr << "Warning: File has parse errors, continuing anyway..." << std::endl;
    }
    
    // Set up refactoring context
    RefactoringContext ctx;
    ctx.tu = tu;
    ctx.source_code = source_code;
    
    // Visit the AST
    CXCursor root_cursor = clang_getTranslationUnitCursor(tu);
    clang_visitChildren(root_cursor, visitorFunction, &ctx);
    
    // Check if any changes were made
    if (ctx.changes_count == 0) {
        std::cout << "No changes needed in " << filepath << std::endl;
        clang_disposeTranslationUnit(tu);
        clang_disposeIndex(index);
        return true;
    }
    
    // Apply replacements
    std::string new_source = applyReplacements(source_code, ctx.replacements);
    
    // Add includes if needed
    for (const auto& include : ctx.includes_needed) {
        new_source = addIncludeIfNeeded(new_source, include, filepath);
    }
    
    // Report changes
    std::cout << "\n" << (dry_run ? "[DRY RUN] " : "") << "Changes in " << filepath << ":" << std::endl;
    std::cout << "  - Converted " << ctx.changes_count << " printf/fprintf calls to log_debug()" << std::endl;
    if (!ctx.includes_needed.empty()) {
        std::string log_path = getRelativeLogInclude(filepath);
        std::cout << "  - Added #include \"" << log_path << "\"" << std::endl;
    }
    
    if (dry_run) {
        std::cout << "\n--- Preview (first few changes) ---" << std::endl;
        // Show preview of changes
        auto old_lines = [&source_code]() {
            std::vector<std::string> lines;
            size_t start = 0;
            size_t end = source_code.find('\n');
            while (end != std::string::npos) {
                lines.push_back(source_code.substr(start, end - start));
                start = end + 1;
                end = source_code.find('\n', start);
            }
            if (start < source_code.length()) {
                lines.push_back(source_code.substr(start));
            }
            return lines;
        }();
        
        auto new_lines = [&new_source]() {
            std::vector<std::string> lines;
            size_t start = 0;
            size_t end = new_source.find('\n');
            while (end != std::string::npos) {
                lines.push_back(new_source.substr(start, end - start));
                start = end + 1;
                end = new_source.find('\n', start);
            }
            if (start < new_source.length()) {
                lines.push_back(new_source.substr(start));
            }
            return lines;
        }();
        
        int shown = 0;
        for (size_t i = 0; i < std::min(old_lines.size(), new_lines.size()) && shown < 5; i++) {
            if (old_lines[i] != new_lines[i]) {
                std::cout << "Line " << (i+1) << ":" << std::endl;
                std::cout << "  - " << old_lines[i] << std::endl;
                std::cout << "  + " << new_lines[i] << std::endl;
                shown++;
            }
        }
    } else {
        // Create backup if requested
        if (backup) {
            std::string backup_path = filepath + ".bak";
            std::ofstream backup_file(backup_path);
            if (backup_file.is_open()) {
                backup_file << source_code;
                backup_file.close();
                std::cout << "  - Backup created: " << backup_path << std::endl;
            } else {
                std::cerr << "Warning: Could not create backup file" << std::endl;
            }
        }
        
        // Write the modified source
        std::ofstream out_file(filepath);
        if (!out_file.is_open()) {
            std::cerr << "Error: Cannot write to file " << filepath << std::endl;
            clang_disposeTranslationUnit(tu);
            clang_disposeIndex(index);
            return false;
        }
        
        out_file << new_source;
        out_file.close();
        std::cout << "  ✓ File updated successfully" << std::endl;
    }
    
    // Clean up
    clang_disposeTranslationUnit(tu);
    clang_disposeIndex(index);
    
    return true;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <source_file> [--dry-run] [--backup]" << std::endl;
        std::cerr << "\nRefactor printf/fprintf calls to log_debug() using Clang AST" << std::endl;
        std::cerr << "\nOptions:" << std::endl;
        std::cerr << "  --dry-run    Show what would be changed without modifying files" << std::endl;
        std::cerr << "  --backup     Create a backup file with .bak extension" << std::endl;
        return 1;
    }
    
    std::string filepath = argv[1];
    bool dry_run = false;
    bool backup = false;
    
    for (int i = 2; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--dry-run") {
            dry_run = true;
        } else if (arg == "--backup") {
            backup = true;
        }
    }
    
    bool success = processFile(filepath, dry_run, backup);
    return success ? 0 : 1;
}
