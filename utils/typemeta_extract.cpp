/**
 * typemeta_extract.cpp - Clang LibTooling based type metadata extractor
 *
 * This tool uses Clang's AST to extract type information from C/C++ headers
 * and generates TypeMeta definitions for the Lambda memory tracker.
 *
 * Build (requires LLVM/Clang development libraries):
 *   clang++ -std=c++17 typemeta_extract.cpp -o typemeta_extract \
 *       $(llvm-config --cxxflags --ldflags --libs) \
 *       -lclang-cpp -lclang
 *
 * Or with CMake (see utils/CMakeLists.txt)
 *
 * Usage:
 *   ./typemeta_extract lambda/lambda.h -- -std=c11 -I. > generated/typemeta_defs.c
 *   ./typemeta_extract --filter="String|List|Map" lambda/lambda.h radiant/view.hpp -- -std=c++17
 */

#include "clang/AST/AST.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>
#include <map>
#include <memory>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace clang;
using namespace clang::tooling;
using namespace llvm;

// ============================================================================
// Command Line Options
// ============================================================================

static cl::OptionCategory ToolCategory("typemeta-extract options");

static cl::opt<std::string> FilterPattern(
    "filter",
    cl::desc("Regex pattern for type names to include"),
    cl::value_desc("pattern"),
    cl::cat(ToolCategory));

static cl::opt<std::string> ExcludePattern(
    "exclude",
    cl::desc("Regex pattern for type names to exclude"),
    cl::value_desc("pattern"),
    cl::cat(ToolCategory));

static cl::opt<std::string> OutputFile(
    "output",
    cl::desc("Output file (default: stdout)"),
    cl::value_desc("file"),
    cl::cat(ToolCategory));

static cl::opt<bool> GenerateJSON(
    "json",
    cl::desc("Output as JSON instead of C code"),
    cl::cat(ToolCategory));

static cl::opt<bool> Verbose(
    "verbose",
    cl::desc("Verbose output"),
    cl::cat(ToolCategory));

// ============================================================================
// Type Information Structures
// ============================================================================

struct FieldInfo {
    std::string name;
    std::string typeName;
    std::string typeMetaRef;  // Reference to TypeMeta
    uint64_t offset = 0;      // Byte offset
    uint64_t bitOffset = 0;   // Bit offset for bitfields
    uint64_t bitWidth = 0;    // Bit width for bitfields
    uint64_t arraySize = 0;   // Array element count (0 = not array)
    bool isPointer = false;
    bool isArray = false;
    bool isBitfield = false;
    bool isConst = false;
    bool isFlexArray = false;
    std::string countField;   // For dynamic arrays, name of count field
    std::vector<std::string> flags;
};

struct EnumValue {
    std::string name;
    int64_t value;
};

struct TypeInfo {
    std::string name;
    std::string kind;  // "struct", "union", "enum", "typedef"
    uint64_t size = 0;
    uint64_t alignment = 0;
    std::vector<FieldInfo> fields;
    std::vector<EnumValue> enumValues;
    std::string baseType;  // For inheritance-like patterns
    std::string underlyingType;  // For enums
    std::vector<std::string> flags;
    std::string sourceFile;
    unsigned sourceLine = 0;
};

// ============================================================================
// Global State
// ============================================================================

static std::map<std::string, TypeInfo> g_types;
static std::vector<std::string> g_typeOrder;  // For dependency ordering
static std::set<std::string> g_processedFiles;
static std::unique_ptr<std::regex> g_includeFilter;
static std::unique_ptr<std::regex> g_excludeFilter;

// Known Lambda ref-counted types
static const std::set<std::string> g_refCountedTypes = {
    "String", "Container", "List", "Map", "Element", "Array",
    "ArrayInt", "ArrayInt64", "ArrayFloat", "Range", "Decimal"
};

// Known Lambda container types
static const std::set<std::string> g_containerTypes = {
    "List", "Map", "Element", "Array", "ArrayInt", "ArrayInt64", "ArrayFloat"
};

// ============================================================================
// Helper Functions
// ============================================================================

static bool shouldIncludeType(const std::string& name) {
    if (name.empty()) return false;

    // Check exclude filter first
    if (g_excludeFilter && std::regex_match(name, *g_excludeFilter)) {
        return false;
    }

    // Check include filter
    if (g_includeFilter && !std::regex_match(name, *g_includeFilter)) {
        return false;
    }

    return true;
}

static std::string sanitizeName(const std::string& name) {
    std::string result;
    for (char c : name) {
        if (std::isalnum(c) || c == '_') {
            result += c;
        } else {
            result += '_';
        }
    }
    return result;
}

static uint32_t computeTypeId(const std::string& name) {
    // Simple hash for type ID
    uint32_t hash = 0x811c9dc5;  // FNV-1a offset basis
    for (char c : name) {
        hash ^= static_cast<uint32_t>(c);
        hash *= 0x01000193;  // FNV-1a prime
    }
    return hash;
}

static std::string getTypeMetaKind(const std::string& typeName) {
    static const std::map<std::string, std::string> primitiveKinds = {
        {"void", "TYPE_KIND_VOID"},
        {"bool", "TYPE_KIND_BOOL"},
        {"_Bool", "TYPE_KIND_BOOL"},
        {"char", "TYPE_KIND_CHAR"},
        {"signed char", "TYPE_KIND_INT8"},
        {"int8_t", "TYPE_KIND_INT8"},
        {"short", "TYPE_KIND_INT16"},
        {"int16_t", "TYPE_KIND_INT16"},
        {"int", "TYPE_KIND_INT32"},
        {"int32_t", "TYPE_KIND_INT32"},
        {"long", "TYPE_KIND_INT64"},
        {"long long", "TYPE_KIND_INT64"},
        {"int64_t", "TYPE_KIND_INT64"},
        {"unsigned char", "TYPE_KIND_UINT8"},
        {"uint8_t", "TYPE_KIND_UINT8"},
        {"unsigned short", "TYPE_KIND_UINT16"},
        {"uint16_t", "TYPE_KIND_UINT16"},
        {"unsigned int", "TYPE_KIND_UINT32"},
        {"unsigned", "TYPE_KIND_UINT32"},
        {"uint32_t", "TYPE_KIND_UINT32"},
        {"unsigned long", "TYPE_KIND_UINT64"},
        {"unsigned long long", "TYPE_KIND_UINT64"},
        {"uint64_t", "TYPE_KIND_UINT64"},
        {"size_t", "TYPE_KIND_UINT64"},
        {"float", "TYPE_KIND_FLOAT"},
        {"double", "TYPE_KIND_DOUBLE"},
    };

    auto it = primitiveKinds.find(typeName);
    if (it != primitiveKinds.end()) {
        return it->second;
    }
    return "TYPE_KIND_STRUCT";  // Default for unknown types
}

static std::string getTypeMetaRef(const QualType& type, ASTContext& ctx) {
    std::string typeName = type.getAsString();

    // Remove const/volatile qualifiers for the reference
    QualType unqualType = type.getUnqualifiedType();

    // Handle pointers
    if (type->isPointerType()) {
        QualType pointeeType = type->getPointeeType();
        std::string baseRef = getTypeMetaRef(pointeeType, ctx);
        // Create a pointer type reference
        return "&_typemeta_ptr_" + sanitizeName(pointeeType.getAsString());
    }

    // Handle arrays
    if (type->isArrayType()) {
        if (auto* constArray = dyn_cast<ConstantArrayType>(type.getTypePtr())) {
            QualType elemType = constArray->getElementType();
            return getTypeMetaRef(elemType, ctx);
        }
    }

    // Handle basic types
    std::string cleanName = unqualType.getAsString();

    // Map common types to TypeMeta names
    static const std::map<std::string, std::string> typeMap = {
        {"_Bool", "bool"},
        {"signed char", "int8"},
        {"unsigned char", "uint8"},
        {"short", "int16"},
        {"unsigned short", "uint16"},
        {"int", "int32"},
        {"unsigned int", "uint32"},
        {"long", "int64"},
        {"unsigned long", "uint64"},
        {"long long", "int64"},
        {"unsigned long long", "uint64"},
    };

    auto it = typeMap.find(cleanName);
    if (it != typeMap.end()) {
        cleanName = it->second;
    }

    return "&TYPEMETA_" + sanitizeName(cleanName);
}

// ============================================================================
// AST Visitor
// ============================================================================

class TypeMetaVisitor : public RecursiveASTVisitor<TypeMetaVisitor> {
public:
    explicit TypeMetaVisitor(ASTContext* context, SourceManager& sm)
        : context_(context), sm_(sm) {}

    bool VisitRecordDecl(RecordDecl* decl) {
        // Skip forward declarations
        if (!decl->isCompleteDefinition()) {
            return true;
        }

        std::string name = decl->getNameAsString();
        if (!shouldIncludeType(name)) {
            return true;
        }

        // Skip if from system header
        if (sm_.isInSystemHeader(decl->getLocation())) {
            return true;
        }

        // Skip anonymous types
        if (name.empty()) {
            return true;
        }

        // Check if already processed
        if (g_types.find(name) != g_types.end()) {
            return true;
        }

        TypeInfo info;
        info.name = name;
        info.kind = decl->isUnion() ? "union" : "struct";

        // Get size and alignment
        if (decl->isCompleteDefinition()) {
            const ASTRecordLayout& layout = context_->getASTRecordLayout(decl);
            info.size = layout.getSize().getQuantity();
            info.alignment = layout.getAlignment().getQuantity();
        }

        // Get source location
        SourceLocation loc = decl->getLocation();
        if (loc.isValid()) {
            info.sourceFile = sm_.getFilename(loc).str();
            info.sourceLine = sm_.getSpellingLineNumber(loc);
        }

        // Process fields
        for (auto* field : decl->fields()) {
            FieldInfo fieldInfo;
            fieldInfo.name = field->getNameAsString();
            fieldInfo.typeName = field->getType().getAsString();

            // Get field offset
            const ASTRecordLayout& layout = context_->getASTRecordLayout(decl);
            unsigned fieldIndex = field->getFieldIndex();
            fieldInfo.offset = layout.getFieldOffset(fieldIndex) / 8;

            // Check for bitfield
            if (field->isBitField()) {
                fieldInfo.isBitfield = true;
                fieldInfo.bitWidth = field->getBitWidthValue(*context_);
                fieldInfo.bitOffset = layout.getFieldOffset(fieldIndex) % 8;
                fieldInfo.flags.push_back("FIELD_FLAG_BITFIELD");
            }

            // Check for pointer
            if (field->getType()->isPointerType()) {
                fieldInfo.isPointer = true;
                fieldInfo.flags.push_back("FIELD_FLAG_POINTER");
                fieldInfo.flags.push_back("FIELD_FLAG_NULLABLE");

                // Heuristic: if field name contains "items" or type is Item*, mark as owned
                if (fieldInfo.name.find("items") != std::string::npos ||
                    fieldInfo.name.find("data") != std::string::npos) {
                    fieldInfo.flags.push_back("FIELD_FLAG_OWNED");
                }
            }

            // Check for array
            if (field->getType()->isArrayType()) {
                fieldInfo.isArray = true;
                fieldInfo.flags.push_back("FIELD_FLAG_ARRAY");

                if (auto* constArray = dyn_cast<ConstantArrayType>(field->getType().getTypePtr())) {
                    fieldInfo.arraySize = constArray->getSize().getZExtValue();
                } else if (auto* incArray = dyn_cast<IncompleteArrayType>(field->getType().getTypePtr())) {
                    // Flexible array member
                    fieldInfo.isFlexArray = true;
                    fieldInfo.flags.push_back("FIELD_FLAG_FLEX");
                }
            }

            // Check for const
            if (field->getType().isConstQualified()) {
                fieldInfo.isConst = true;
                fieldInfo.flags.push_back("FIELD_FLAG_CONST");
            }

            // Get TypeMeta reference
            fieldInfo.typeMetaRef = getTypeMetaRef(field->getType(), *context_);

            // Heuristic: detect count fields for arrays
            // If a pointer field is followed by a "length" or "count" field
            if (fieldInfo.isPointer) {
                // Look for common count field patterns
                static const std::vector<std::string> countPatterns = {
                    "length", "len", "count", "size", "num", "n"
                };
                for (const auto& pattern : countPatterns) {
                    std::string potentialCountField = fieldInfo.name + "_" + pattern;
                    // Also check for just "length" after an "items" field
                    if (fieldInfo.name == "items" || fieldInfo.name == "data") {
                        fieldInfo.countField = "length";
                        break;
                    }
                }
            }

            info.fields.push_back(fieldInfo);
        }

        // Determine type flags
        if (g_refCountedTypes.count(name)) {
            info.flags.push_back("TYPE_FLAG_REFCOUNTED");
        }
        if (g_containerTypes.count(name)) {
            info.flags.push_back("TYPE_FLAG_CONTAINER");
        }

        // Detect base type (look for first field that's another struct)
        if (!info.fields.empty()) {
            const FieldInfo& firstField = info.fields[0];
            // Common pattern: first few fields match Container layout
            if (firstField.name == "type_id" && info.fields.size() > 2) {
                if (info.fields[1].name == "flags" && info.fields[2].name == "ref_cnt") {
                    info.baseType = "Container";
                }
            }
        }

        g_types[name] = info;
        g_typeOrder.push_back(name);

        if (Verbose) {
            llvm::errs() << "Extracted: " << info.kind << " " << name
                        << " (size=" << info.size << ", fields=" << info.fields.size() << ")\n";
        }

        return true;
    }

    bool VisitEnumDecl(EnumDecl* decl) {
        std::string name = decl->getNameAsString();
        if (!shouldIncludeType(name)) {
            return true;
        }

        if (sm_.isInSystemHeader(decl->getLocation())) {
            return true;
        }

        if (name.empty() || g_types.find(name) != g_types.end()) {
            return true;
        }

        TypeInfo info;
        info.name = name;
        info.kind = "enum";

        // Get size from the integer type
        QualType intType = decl->getIntegerType();
        if (!intType.isNull()) {
            info.size = context_->getTypeSize(intType) / 8;
            info.alignment = context_->getTypeAlign(intType) / 8;
            info.underlyingType = intType.getAsString();
        } else {
            info.size = 4;
            info.alignment = 4;
            info.underlyingType = "int";
        }

        // Get source location
        SourceLocation loc = decl->getLocation();
        if (loc.isValid()) {
            info.sourceFile = sm_.getFilename(loc).str();
            info.sourceLine = sm_.getSpellingLineNumber(loc);
        }

        // Extract enum values
        for (auto* enumerator : decl->enumerators()) {
            EnumValue ev;
            ev.name = enumerator->getNameAsString();
            ev.value = enumerator->getInitVal().getExtValue();
            info.enumValues.push_back(ev);
        }

        g_types[name] = info;
        g_typeOrder.push_back(name);

        if (Verbose) {
            llvm::errs() << "Extracted: enum " << name
                        << " (values=" << info.enumValues.size() << ")\n";
        }

        return true;
    }

    bool VisitTypedefDecl(TypedefDecl* decl) {
        std::string name = decl->getNameAsString();
        if (!shouldIncludeType(name)) {
            return true;
        }

        if (sm_.isInSystemHeader(decl->getLocation())) {
            return true;
        }

        // Handle typedef to struct/enum - create alias
        QualType underlying = decl->getUnderlyingType();
        std::string underlyingName = underlying.getAsString();

        // Remove "struct " or "enum " prefix
        if (underlyingName.rfind("struct ", 0) == 0) {
            underlyingName = underlyingName.substr(7);
        } else if (underlyingName.rfind("enum ", 0) == 0) {
            underlyingName = underlyingName.substr(5);
        }

        // If the underlying type is already extracted, create an alias
        if (g_types.find(underlyingName) != g_types.end() && name != underlyingName) {
            // Just note this as an alias - could be used for generating typedef aliases
            if (Verbose) {
                llvm::errs() << "Typedef: " << name << " -> " << underlyingName << "\n";
            }
        }

        return true;
    }

private:
    ASTContext* context_;
    SourceManager& sm_;
};

// ============================================================================
// AST Consumer
// ============================================================================

class TypeMetaConsumer : public ASTConsumer {
public:
    explicit TypeMetaConsumer(ASTContext* context, SourceManager& sm)
        : visitor_(context, sm) {}

    void HandleTranslationUnit(ASTContext& context) override {
        visitor_.TraverseDecl(context.getTranslationUnitDecl());
    }

private:
    TypeMetaVisitor visitor_;
};

// ============================================================================
// Frontend Action
// ============================================================================

class TypeMetaAction : public ASTFrontendAction {
public:
    std::unique_ptr<ASTConsumer> CreateASTConsumer(
        CompilerInstance& ci, StringRef file) override {
        return std::make_unique<TypeMetaConsumer>(
            &ci.getASTContext(), ci.getSourceManager());
    }
};

// ============================================================================
// Code Generation
// ============================================================================

static void generateCHeader(raw_ostream& os) {
    os << "// =============================================================================\n";
    os << "// Auto-generated by typemeta_extract - DO NOT EDIT\n";
    os << "// =============================================================================\n";
    os << "//\n";
    os << "// Generated from:\n";
    for (const auto& file : g_processedFiles) {
        os << "//   " << file << "\n";
    }
    os << "//\n";
    os << "// Regenerate with:\n";
    os << "//   ./typemeta_extract --output=<file> <headers> -- <clang-args>\n";
    os << "//\n\n";

    os << "#include \"typemeta.h\"\n\n";

    os << "#ifdef __cplusplus\n";
    os << "extern \"C\" {\n";
    os << "#endif\n\n";
}

static void generatePrimitives(raw_ostream& os) {
    os << "// =============================================================================\n";
    os << "// Primitive Types\n";
    os << "// =============================================================================\n\n";

    struct PrimDef {
        const char* name;
        const char* kind;
        const char* ctype;
    };

    static const PrimDef primitives[] = {
        {"void",   "TYPE_KIND_VOID",   "char"},  // void has no size
        {"bool",   "TYPE_KIND_BOOL",   "bool"},
        {"char",   "TYPE_KIND_CHAR",   "char"},
        {"int8",   "TYPE_KIND_INT8",   "int8_t"},
        {"int16",  "TYPE_KIND_INT16",  "int16_t"},
        {"int32",  "TYPE_KIND_INT32",  "int32_t"},
        {"int64",  "TYPE_KIND_INT64",  "int64_t"},
        {"uint8",  "TYPE_KIND_UINT8",  "uint8_t"},
        {"uint16", "TYPE_KIND_UINT16", "uint16_t"},
        {"uint32", "TYPE_KIND_UINT32", "uint32_t"},
        {"uint64", "TYPE_KIND_UINT64", "uint64_t"},
        {"float",  "TYPE_KIND_FLOAT",  "float"},
        {"double", "TYPE_KIND_DOUBLE", "double"},
    };

    for (const auto& p : primitives) {
        uint32_t typeId = computeTypeId(p.name);
        if (std::string(p.name) == "void") {
            os << "const TypeMeta TYPEMETA_" << p.name << " = { "
               << "\"" << p.name << "\", " << p.kind << ", 0, 1, "
               << "0x" << llvm::format_hex_no_prefix(typeId, 8) << ", 0 };\n";
        } else {
            os << "const TypeMeta TYPEMETA_" << p.name << " = { "
               << "\"" << p.name << "\", " << p.kind << ", sizeof(" << p.ctype << "), "
               << "_Alignof(" << p.ctype << "), "
               << "0x" << llvm::format_hex_no_prefix(typeId, 8) << ", 0 };\n";
        }
    }
    os << "\n";
}

static void generatePointerTypes(raw_ostream& os) {
    os << "// =============================================================================\n";
    os << "// Pointer Types\n";
    os << "// =============================================================================\n\n";

    // Collect all unique pointer target types
    std::set<std::string> pointerTargets;
    for (const auto& [name, info] : g_types) {
        for (const auto& field : info.fields) {
            if (field.isPointer) {
                // Extract base type from pointer type
                std::string baseType = field.typeName;
                size_t starPos = baseType.find('*');
                if (starPos != std::string::npos) {
                    baseType = baseType.substr(0, starPos);
                }
                // Remove const/volatile
                baseType.erase(std::remove_if(baseType.begin(), baseType.end(), ::isspace), baseType.end());
                if (baseType.rfind("const", 0) == 0) baseType = baseType.substr(5);
                if (baseType.rfind("volatile", 0) == 0) baseType = baseType.substr(8);

                // Trim whitespace
                size_t start = baseType.find_first_not_of(" \t");
                size_t end = baseType.find_last_not_of(" \t");
                if (start != std::string::npos) {
                    baseType = baseType.substr(start, end - start + 1);
                }

                if (!baseType.empty()) {
                    pointerTargets.insert(baseType);
                }
            }
        }
    }

    for (const auto& target : pointerTargets) {
        std::string safeName = sanitizeName(target);
        uint32_t typeId = computeTypeId(target + "*");

        os << "static const TypeMeta _typemeta_ptr_" << safeName << " = {\n";
        os << "    .name = \"" << target << "*\",\n";
        os << "    .kind = TYPE_KIND_POINTER,\n";
        os << "    .size = sizeof(void*),\n";
        os << "    .alignment = _Alignof(void*),\n";
        os << "    .type_id = 0x" << llvm::format_hex_no_prefix(typeId, 8) << ",\n";
        os << "    .pointer = { .target_type = &TYPEMETA_" << safeName << " },\n";
        os << "};\n\n";
    }
}

static void generateField(raw_ostream& os, const std::string& structName, const FieldInfo& field) {
    os << "    {\n";
    os << "        .name = \"" << field.name << "\",\n";
    os << "        .type = " << field.typeMetaRef << ",\n";
    os << "        .offset = offsetof(" << structName << ", " << field.name << "),\n";
    os << "        .bit_offset = " << field.bitOffset << ",\n";
    os << "        .bit_width = " << field.bitWidth << ",\n";

    if (field.flags.empty()) {
        os << "        .flags = 0,\n";
    } else {
        os << "        .flags = ";
        for (size_t i = 0; i < field.flags.size(); ++i) {
            if (i > 0) os << " | ";
            os << field.flags[i];
        }
        os << ",\n";
    }

    os << "        .array_count = " << field.arraySize << ",\n";

    if (!field.countField.empty()) {
        os << "        .count_field = \"" << field.countField << "\",\n";
    } else {
        os << "        .count_field = NULL,\n";
    }

    os << "    },\n";
}

static void generateStruct(raw_ostream& os, const TypeInfo& info) {
    std::string safeName = sanitizeName(info.name);
    uint32_t typeId = computeTypeId(info.name);

    // Write fields array
    if (!info.fields.empty()) {
        os << "static const FieldMeta _typemeta_fields_" << safeName << "[] = {\n";
        for (const auto& field : info.fields) {
            generateField(os, info.name, field);
        }
        os << "};\n\n";
    }

    // Write type metadata
    os << "const TypeMeta TYPEMETA_" << safeName << " = {\n";
    os << "    .name = \"" << info.name << "\",\n";
    os << "    .kind = " << (info.kind == "union" ? "TYPE_KIND_UNION" : "TYPE_KIND_STRUCT") << ",\n";
    os << "    .size = sizeof(" << info.name << "),\n";
    os << "    .alignment = _Alignof(" << info.name << "),\n";
    os << "    .type_id = 0x" << llvm::format_hex_no_prefix(typeId, 8) << ",\n";

    if (info.flags.empty()) {
        os << "    .flags = 0,\n";
    } else {
        os << "    .flags = ";
        for (size_t i = 0; i < info.flags.size(); ++i) {
            if (i > 0) os << " | ";
            os << info.flags[i];
        }
        os << ",\n";
    }

    if (!info.fields.empty()) {
        os << "    .composite = {\n";
        os << "        .fields = _typemeta_fields_" << safeName << ",\n";
        os << "        .field_count = sizeof(_typemeta_fields_" << safeName << ") / sizeof(FieldMeta),\n";
        if (!info.baseType.empty()) {
            os << "        .base_type = &TYPEMETA_" << sanitizeName(info.baseType) << ",\n";
        } else {
            os << "        .base_type = NULL,\n";
        }
        os << "    },\n";
    }

    os << "};\n\n";
}

static void generateEnum(raw_ostream& os, const TypeInfo& info) {
    std::string safeName = sanitizeName(info.name);
    uint32_t typeId = computeTypeId(info.name);

    // Write enum values array
    if (!info.enumValues.empty()) {
        os << "static const EnumValueMeta _typemeta_values_" << safeName << "[] = {\n";
        for (const auto& ev : info.enumValues) {
            os << "    { \"" << ev.name << "\", " << ev.value << " },\n";
        }
        os << "};\n\n";
    }

    // Map underlying type to TypeMeta
    std::string underlyingRef = "&TYPEMETA_int32";  // Default
    if (info.underlyingType == "unsigned char" || info.underlyingType == "uint8_t") {
        underlyingRef = "&TYPEMETA_uint8";
    } else if (info.underlyingType == "unsigned short" || info.underlyingType == "uint16_t") {
        underlyingRef = "&TYPEMETA_uint16";
    } else if (info.underlyingType == "unsigned int" || info.underlyingType == "uint32_t") {
        underlyingRef = "&TYPEMETA_uint32";
    }

    // Write type metadata
    os << "const TypeMeta TYPEMETA_" << safeName << " = {\n";
    os << "    .name = \"" << info.name << "\",\n";
    os << "    .kind = TYPE_KIND_ENUM,\n";
    os << "    .size = sizeof(" << info.name << "),\n";
    os << "    .alignment = _Alignof(" << info.name << "),\n";
    os << "    .type_id = 0x" << llvm::format_hex_no_prefix(typeId, 8) << ",\n";
    os << "    .flags = 0,\n";

    if (!info.enumValues.empty()) {
        os << "    .enum_info = {\n";
        os << "        .values = _typemeta_values_" << safeName << ",\n";
        os << "        .value_count = sizeof(_typemeta_values_" << safeName << ") / sizeof(EnumValueMeta),\n";
        os << "        .underlying_type = " << underlyingRef << ",\n";
        os << "    },\n";
    }

    os << "};\n\n";
}

static void generateTypes(raw_ostream& os) {
    os << "// =============================================================================\n";
    os << "// Composite Types\n";
    os << "// =============================================================================\n\n";

    for (const auto& name : g_typeOrder) {
        const TypeInfo& info = g_types[name];

        os << "// " << info.kind << " " << info.name;
        if (!info.sourceFile.empty()) {
            os << " from " << info.sourceFile << ":" << info.sourceLine;
        }
        os << "\n";

        if (info.kind == "enum") {
            generateEnum(os, info);
        } else {
            generateStruct(os, info);
        }
    }
}

static void generateRegistration(raw_ostream& os) {
    os << "// =============================================================================\n";
    os << "// Type Registration\n";
    os << "// =============================================================================\n\n";

    os << "void typemeta_register_generated(void) {\n";

    // Register primitives
    os << "    // Primitives\n";
    const char* primitives[] = {
        "void", "bool", "char", "int8", "int16", "int32", "int64",
        "uint8", "uint16", "uint32", "uint64", "float", "double"
    };
    for (const char* p : primitives) {
        os << "    typemeta_register(&TYPEMETA_" << p << ");\n";
    }

    os << "\n    // Generated types\n";
    for (const auto& name : g_typeOrder) {
        os << "    typemeta_register(&TYPEMETA_" << sanitizeName(name) << ");\n";
    }

    os << "}\n";
}

static void generateCFooter(raw_ostream& os) {
    os << "\n#ifdef __cplusplus\n";
    os << "}\n";
    os << "#endif\n";
}

static void generateJSON(raw_ostream& os) {
    os << "{\n";
    os << "  \"types\": [\n";

    bool firstType = true;
    for (const auto& name : g_typeOrder) {
        const TypeInfo& info = g_types[name];

        if (!firstType) os << ",\n";
        firstType = false;

        os << "    {\n";
        os << "      \"name\": \"" << info.name << "\",\n";
        os << "      \"kind\": \"" << info.kind << "\",\n";
        os << "      \"size\": " << info.size << ",\n";
        os << "      \"alignment\": " << info.alignment << ",\n";

        if (!info.sourceFile.empty()) {
            os << "      \"source\": \"" << info.sourceFile << ":" << info.sourceLine << "\",\n";
        }

        if (!info.fields.empty()) {
            os << "      \"fields\": [\n";
            bool firstField = true;
            for (const auto& field : info.fields) {
                if (!firstField) os << ",\n";
                firstField = false;

                os << "        {\n";
                os << "          \"name\": \"" << field.name << "\",\n";
                os << "          \"type\": \"" << field.typeName << "\",\n";
                os << "          \"offset\": " << field.offset << ",\n";
                os << "          \"is_pointer\": " << (field.isPointer ? "true" : "false") << ",\n";
                os << "          \"is_bitfield\": " << (field.isBitfield ? "true" : "false");
                if (field.isBitfield) {
                    os << ",\n          \"bit_width\": " << field.bitWidth;
                }
                os << "\n        }";
            }
            os << "\n      ],\n";
        }

        if (!info.enumValues.empty()) {
            os << "      \"values\": [\n";
            bool firstVal = true;
            for (const auto& ev : info.enumValues) {
                if (!firstVal) os << ",\n";
                firstVal = false;
                os << "        { \"name\": \"" << ev.name << "\", \"value\": " << ev.value << " }";
            }
            os << "\n      ],\n";
        }

        os << "      \"type_id\": \"0x" << llvm::format_hex_no_prefix(computeTypeId(info.name), 8) << "\"\n";
        os << "    }";
    }

    os << "\n  ]\n";
    os << "}\n";
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, const char** argv) {
    auto expectedParser = CommonOptionsParser::create(argc, argv, ToolCategory);
    if (!expectedParser) {
        llvm::errs() << expectedParser.takeError();
        return 1;
    }
    CommonOptionsParser& optParser = expectedParser.get();

    // Set up filters
    if (!FilterPattern.empty()) {
        try {
            g_includeFilter = std::make_unique<std::regex>(FilterPattern.getValue());
        } catch (const std::regex_error& e) {
            llvm::errs() << "Invalid filter pattern: " << e.what() << "\n";
            return 1;
        }
    }

    if (!ExcludePattern.empty()) {
        try {
            g_excludeFilter = std::make_unique<std::regex>(ExcludePattern.getValue());
        } catch (const std::regex_error& e) {
            llvm::errs() << "Invalid exclude pattern: " << e.what() << "\n";
            return 1;
        }
    }

    // Record processed files
    for (const auto& file : optParser.getSourcePathList()) {
        g_processedFiles.insert(file);
    }

    // Run the tool
    ClangTool tool(optParser.getCompilations(), optParser.getSourcePathList());
    int result = tool.run(newFrontendActionFactory<TypeMetaAction>().get());

    if (result != 0) {
        llvm::errs() << "Clang tool failed\n";
        return result;
    }

    // Generate output
    std::error_code ec;
    std::unique_ptr<raw_fd_ostream> fileOs;
    raw_ostream* os = &llvm::outs();

    if (!OutputFile.empty()) {
        fileOs = std::make_unique<raw_fd_ostream>(OutputFile, ec);
        if (ec) {
            llvm::errs() << "Error opening output file: " << ec.message() << "\n";
            return 1;
        }
        os = fileOs.get();
    }

    if (GenerateJSON) {
        generateJSON(*os);
    } else {
        generateCHeader(*os);
        generatePrimitives(*os);
        generatePointerTypes(*os);
        generateTypes(*os);
        generateRegistration(*os);
        generateCFooter(*os);
    }

    llvm::errs() << "Extracted " << g_types.size() << " types\n";

    return 0;
}
