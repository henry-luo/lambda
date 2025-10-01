# LaTeX Typeset Refactoring Plan - Radiant Integration (Stage 3)

## Overview

This document outlines a comprehensive plan to refactor the Lambda typeset module to integrate with Radiant's view system. The goal is to leverage Radiant's production-ready rendering capabilities (screen, SVG, PDF, images) while maintaining the same LaTeX parsing and typesetting flow through Lambda.

## Current Architecture Analysis

### Existing Typeset System
- **Location**: `./typeset/` directory
- **Core Components**:
  - `typeset.h/c` - Main typeset engine
  - `view/view_tree.h` - Custom ViewTree system with mathematical typesetting support
  - `latex_typeset.h/c` - LaTeX-specific typesetting functions
  - `integration/latex_bridge.cpp` - Lambda AST to ViewTree conversion
  - `output/` - Multiple renderers (SVG, PDF, HTML)

### Radiant View System
- **Location**: `./radiant/` directory  
- **Core Components**:
  - `view.hpp` - Radiant's ViewBlock/ViewSpan/ViewText system
  - `layout*.cpp` - Production-ready layout engines (block, flex, grid, table, positioning)
  - `render*.cpp` - High-performance renderers (screen, SVG, PDF, images)
  - `window.cpp` - CLI interface with `layout` and `render` subcommands

### Key Differences
1. **View Structures**:
   - Typeset: `ViewTree` → `ViewNode` (mathematical focus, device-independent)
   - Radiant: `ViewTree` → `ViewBlock/ViewSpan/ViewText` (web layout focus, pixel-based)

2. **Rendering Capabilities**:
   - Typeset: Basic PDF/SVG output via libharu
   - Radiant: Production-ready multi-format rendering with advanced features

3. **Layout Engines**:
   - Typeset: Basic block layout with math-specific positioning
   - Radiant: Complete CSS layout system (flexbox, grid, table, positioning)

## Refactoring Strategy

### Phase 1: Analysis and Planning (Week 1)
**Objective**: Understand integration points and design new architecture

#### 1.1 Architecture Design
**New Directory Structure**:
```
radiant/typeset/
├── typeset_engine.cpp/hpp        # Main typeset engine (Radiant-integrated)
├── latex_bridge.cpp/hpp          # LaTeX AST to Radiant view conversion
├── math_layout.cpp/hpp           # Mathematical layout using Radiant views
├── document_structure.cpp/hpp    # LaTeX document structure handling
├── text_formatting.cpp/hpp       # LaTeX text formatting
└── math_symbols.cpp/hpp          # Mathematical symbol rendering
```

#### 1.2 Integration Points Analysis
**Lambda Parser Integration**:
- Keep existing `lambda/input/input-latex.cpp` for LaTeX parsing
- Reuse existing `lambda/input/input-math.cpp` for mathematical expressions
- Maintain Lambda AST structure as intermediate representation
- Bridge Lambda AST to Radiant views instead of typeset ViewTree

**Radiant View Extensions for LaTeX**:
```cpp
// Extend existing Radiant views for LaTeX-specific features
ViewSpan + math_content    → LaTeX Math Inline (reuse ViewSpan)
ViewBlock + math_display   → LaTeX Math Display (reuse ViewBlock)
ViewText + latex_formatting → LaTeX formatted text (reuse ViewText)

// New minimal math-specific view (only when absolutely necessary)
typedef struct ViewMath : ViewSpan {
    char* math_expression;     // LaTeX/ASCIIMath expression
    bool is_display_mode;      // inline vs display math
    double baseline_offset;    // mathematical baseline adjustment
    char* math_font_family;    // math-specific font
} ViewMath;

// Reuse existing views wherever possible
LaTeX Document     → ViewBlock (document container)
LaTeX Section      → ViewBlock (with heading styles)
LaTeX Paragraph    → ViewBlock (with text content)
LaTeX Table        → ViewTable/ViewTableRow/ViewTableCell (existing)
LaTeX List         → ViewBlock (with list styling)
```

### Phase 2: Core Infrastructure (Week 2)
**Objective**: Create Radiant-based typeset engine foundation

#### 2.1 Radiant Typeset Engine
**File**: `radiant/typeset/typeset_engine.cpp/hpp`

```cpp
// Radiant-integrated typeset engine
class RadiantTypesetEngine {
private:
    UiContext* ui_context;           // Radiant UI context
    VariableMemPool* pool;           // Memory management
    TypesetOptions* options;         // Typeset configuration
    
public:
    // Main typesetting function
    ViewTree* typeset_latex_document(Item latex_ast, TypesetOptions* options);
    
    // LaTeX-specific processing
    ViewBlock* process_latex_document(Item document_node);
    ViewBlock* process_latex_section(Item section_node, int level);
    ViewBlock* process_latex_paragraph(Item paragraph_node);
    ViewSpan* process_latex_math_inline(Item math_node);
    ViewBlock* process_latex_math_display(Item math_node);
    ViewTable* process_latex_table(Item table_node);
    ViewBlock* process_latex_list(Item list_node);
    
    // Text formatting
    ViewSpan* process_text_formatting(Item text_node, const char* format_type);
    ViewSpan* apply_font_styling(ViewSpan* span, const char* font_command);
    
    // Integration with Radiant layout
    void apply_latex_styling(ViewBlock* view, const char* latex_class);
    void setup_page_layout(ViewTree* tree, TypesetOptions* options);
};
```

#### 2.2 Lambda Bridge Adapter
**File**: `radiant/typeset/latex_bridge.cpp/hpp`

```cpp
// Bridge Lambda AST to Radiant views (reusing existing view system)
class LaTeXRadiantBridge {
private:
    RadiantTypesetEngine* engine;
    VariableMemPool* pool;
    
public:
    // Main conversion function
    ViewTree* convert_latex_ast_to_radiant(Item latex_ast);
    
    // Element-specific conversions (reuse existing views)
    ViewBlock* convert_document_element(Item element);
    ViewBlock* convert_section_element(Item element);
    ViewBlock* convert_paragraph_element(Item element);
    ViewSpan* convert_text_element(Item element);
    ViewSpan* convert_math_inline(Item math_element);      // Reuse ViewSpan
    ViewBlock* convert_math_display(Item math_element);    // Reuse ViewBlock
    ViewTable* convert_table_element(Item element);        // Reuse existing ViewTable
    
    // Math integration with existing input-math.cpp
    ViewSpan* process_math_expression(const char* math_content, bool is_display);
    void integrate_with_input_math(Item math_ast);
    
    // Utility functions
    void apply_latex_attributes(View* view, Item element);
    void setup_document_metadata(ViewTree* tree, Item document);
    void process_latex_preamble(Item preamble, TypesetOptions* options);
};
```

### Phase 3: Mathematical Typesetting (Week 3)
**Objective**: Implement LaTeX math using Radiant's view system

#### 3.1 Mathematical Layout Engine
**File**: `radiant/typeset/math_layout.cpp/hpp`

```cpp
// Mathematical typesetting using existing Radiant views + minimal extensions
class MathLayoutEngine {
private:
    UiContext* ui_context;
    FontProp* math_font;
    FontProp* text_font;
    
public:
    // Main math processing (reuse existing input-math.cpp)
    ViewSpan* layout_math_expression(Item math_ast, bool is_display_mode);
    
    // Math element types (extend existing ViewSpan/ViewBlock)
    ViewSpan* layout_math_fraction(Item numerator, Item denominator);    // Use nested ViewSpan
    ViewSpan* layout_math_superscript(Item base, Item superscript);      // Use positioned ViewSpan
    ViewSpan* layout_math_subscript(Item base, Item subscript);          // Use positioned ViewSpan
    ViewSpan* layout_math_radical(Item radicand, Item index);            // Use ViewSpan with special styling
    ViewTable* layout_math_matrix(Item matrix_node);                     // Reuse existing ViewTable
    ViewSpan* layout_math_delimiter(Item content, const char* left, const char* right);
    
    // Mathematical symbols (extend existing text rendering)
    ViewText* create_math_symbol(const char* latex_command);             // Reuse ViewText
    ViewText* create_math_operator(const char* operator_name);           // Reuse ViewText
    ViewText* create_math_function(const char* function_name);           // Reuse ViewText
    
    // Math spacing and positioning (extend existing layout)
    void apply_math_spacing(ViewSpan* math_span, const char* spacing_type);
    void position_math_elements(ViewSpan* container, ViewSpan** elements, int count);
    
    // Font and sizing (extend existing FontProp)
    FontProp* get_math_font(int math_style, double base_size);
    void apply_math_font_sizing(ViewSpan* span, int math_style);
    
    // Integration with input-math.cpp
    Item parse_math_with_existing_parser(const char* math_content, const char* flavor);
    ViewSpan* convert_math_ast_to_view(Item math_ast);
};
```

#### 3.2 Mathematical Symbol System
**File**: `radiant/typeset/math_symbols.cpp/hpp`

```cpp
// Mathematical symbol rendering (extend existing text system)
class MathSymbolRenderer {
private:
    struct MathSymbolDef {
        const char* latex_command;
        const char* unicode_char;
        const char* font_family;
        double relative_size;
        int symbol_class;  // operator, relation, binary, etc.
    };
    
    static MathSymbolDef symbol_table[];
    
public:
    // Symbol lookup and creation (reuse ViewText)
    ViewText* create_symbol(const char* latex_command, FontProp* font);      // Reuse ViewText
    const char* get_unicode_for_symbol(const char* latex_command);
    int get_symbol_class(const char* latex_command);
    
    // Special symbol handling (extend existing views)
    ViewSpan* create_large_operator(const char* operator_name, bool display_mode);  // ViewSpan container
    ViewSpan* create_delimiter(const char* delimiter, double height);               // ViewSpan with scaling
    ViewSpan* create_accent(const char* accent_type, ViewSpan* base);               // Positioned ViewSpan
    
    // Greek letters and special characters (reuse text rendering)
    ViewText* create_greek_letter(const char* letter_name, bool uppercase);         // ViewText with Unicode
    ViewText* create_special_character(const char* char_name);                      // ViewText with Unicode
    
    // Integration with existing font system
    void extend_font_system_for_math(UiContext* ui_context);
    FontProp* get_math_font_variant(FontProp* base_font, const char* variant);
};
```

### Phase 4: Document Structure (Week 4)
**Objective**: Implement LaTeX document structure using Radiant layout

#### 4.1 Document Structure Handler
**File**: `radiant/typeset/document_structure.cpp/hpp`

```cpp
// LaTeX document structure processing
class DocumentStructureProcessor {
private:
    RadiantTypesetEngine* engine;
    int section_counters[6];  // part, chapter, section, subsection, etc.
    
public:
    // Document-level processing
    ViewBlock* process_document_class(Item document_class, TypesetOptions* options);
    ViewBlock* process_document_title(Item title_node);
    ViewBlock* process_document_author(Item author_node);
    ViewBlock* process_maketitle(Item maketitle_node);
    ViewBlock* process_abstract(Item abstract_node);
    
    // Sectioning
    ViewBlock* process_section(Item section_node, int level);
    ViewBlock* process_chapter(Item chapter_node);
    ViewBlock* generate_table_of_contents(ViewTree* tree);
    
    // Numbering system
    void update_section_numbering(int level);
    char* get_section_number(int level);
    void setup_equation_numbering(ViewTree* tree);
    
    // Page layout
    void setup_page_geometry(ViewTree* tree, TypesetOptions* options);
    void add_headers_footers(ViewTree* tree, TypesetOptions* options);
};
```

#### 4.2 Text Formatting Engine
**File**: `radiant/typeset/text_formatting.cpp/hpp`

```cpp
// LaTeX text formatting
class TextFormattingEngine {
private:
    UiContext* ui_context;
    
public:
    // Font styling
    ViewSpan* apply_bold_formatting(ViewSpan* text);
    ViewSpan* apply_italic_formatting(ViewSpan* text);
    ViewSpan* apply_typewriter_formatting(ViewSpan* text);
    ViewSpan* apply_small_caps_formatting(ViewSpan* text);
    
    // Font sizing
    ViewSpan* apply_font_size(ViewSpan* text, const char* size_command);
    double get_font_size_multiplier(const char* size_command);
    
    // Text environments
    ViewBlock* process_quote_environment(Item quote_node);
    ViewBlock* process_verbatim_environment(Item verbatim_node);
    ViewBlock* process_center_environment(Item center_node);
    
    // Special characters and accents
    ViewSpan* process_special_character(const char* char_command);
    ViewSpan* process_accent(const char* accent_command, ViewSpan* base);
    
    // Line and paragraph formatting
    void apply_line_spacing(ViewBlock* paragraph, double line_height);
    void apply_paragraph_indentation(ViewBlock* paragraph, double indent);
    void apply_text_alignment(ViewBlock* block, const char* alignment);
};
```

### Phase 5: Advanced Features (Week 5)
**Objective**: Implement tables, lists, and cross-references

#### 5.1 Table Processing
**File**: `radiant/typeset/table_processor.cpp/hpp`

```cpp
// LaTeX table processing using Radiant table system
class LaTeXTableProcessor {
private:
    RadiantTypesetEngine* engine;
    
public:
    // Table environments
    ViewTable* process_tabular_environment(Item tabular_node);
    ViewBlock* process_table_environment(Item table_node);  // floating table
    
    // Table structure
    ViewTableRow* process_table_row(Item row_node);
    ViewTableCell* process_table_cell(Item cell_node, const char* column_spec);
    
    // Column specifications
    struct ColumnSpec {
        enum { LEFT, CENTER, RIGHT, PARAGRAPH } alignment;
        double width;  // for paragraph columns
        bool has_left_border;
        bool has_right_border;
    };
    
    ColumnSpec* parse_column_specification(const char* col_spec);
    void apply_column_styling(ViewTableCell* cell, ColumnSpec* spec);
    
    // Table features
    void process_hline(ViewTable* table, int row_index);
    void process_cline(ViewTable* table, int row_index, int start_col, int end_col);
    ViewTableCell* process_multicolumn(Item multicolumn_node);
    
    // Table layout integration
    void setup_table_layout(ViewTable* table, ColumnSpec* columns, int col_count);
    void apply_table_borders(ViewTable* table, bool border_collapse);
};
```

#### 5.2 List Processing
**File**: `radiant/typeset/list_processor.cpp/hpp`

```cpp
// LaTeX list processing
class LaTeXListProcessor {
private:
    RadiantTypesetEngine* engine;
    int list_depth;
    
public:
    // List environments
    ViewBlock* process_itemize_environment(Item itemize_node);
    ViewBlock* process_enumerate_environment(Item enumerate_node);
    ViewBlock* process_description_environment(Item description_node);
    
    // List items
    ViewBlock* process_list_item(Item item_node, const char* list_type, int item_number);
    ViewSpan* create_list_marker(const char* list_type, int item_number, int depth);
    
    // List styling
    void apply_list_indentation(ViewBlock* list, int depth);
    void apply_list_spacing(ViewBlock* list, double item_spacing);
    
    // Custom list features
    ViewSpan* create_custom_bullet(const char* bullet_symbol);
    ViewSpan* create_numbered_marker(int number, const char* format);
    ViewSpan* create_description_term(Item term_node);
};
```

### Phase 6: Integration and CLI (Week 6)
**Objective**: Integrate with Radiant's CLI and rendering system

#### 6.1 CLI Integration
**File**: `radiant/window.cpp` (enhancement)

```cpp
// Enhanced CLI with LaTeX support
class RadiantCLI {
private:
    RadiantTypesetEngine* typeset_engine;
    
public:
    // Enhanced layout command
    int cmd_layout(int argc, char** argv) {
        // Existing HTML support
        if (is_html_file(input_file)) {
            return layout_html_file(input_file, output_file);
        }
        
        // NEW: LaTeX support
        if (is_latex_file(input_file)) {
            return layout_latex_file(input_file, output_file);
        }
        
        return error_unsupported_format();
    }
    
    // Enhanced render command
    int cmd_render(int argc, char** argv) {
        // Existing HTML rendering
        if (is_html_file(input_file)) {
            return render_html_file(input_file, output_file, format);
        }
        
        // NEW: LaTeX rendering
        if (is_latex_file(input_file)) {
            return render_latex_file(input_file, output_file, format);
        }
        
        return error_unsupported_format();
    }
    
private:
    // LaTeX-specific CLI functions
    int layout_latex_file(const char* input_file, const char* output_file);
    int render_latex_file(const char* input_file, const char* output_file, const char* format);
    bool is_latex_file(const char* filename);
    
    // LaTeX processing pipeline
    ViewTree* parse_and_layout_latex(const char* latex_file);
    int render_latex_to_format(ViewTree* tree, const char* output_file, const char* format);
};
```

#### 6.2 Rendering Extensions (No New Pipeline)
**Files**: Extend existing `radiant/render*.cpp` files

```cpp
// Extend existing render_pdf.cpp for LaTeX math
void extend_pdf_renderer_for_math() {
    // Add math font support to existing PDF renderer
    // Add mathematical symbol rendering
    // Add math spacing and positioning
}

// Extend existing render_svg.cpp for LaTeX math  
void extend_svg_renderer_for_math() {
    // Add MathML output support
    // Add mathematical symbol SVG paths
    // Add math layout preservation
}

// Extend existing render.cpp for screen rendering
void extend_screen_renderer_for_math() {
    // Add math font rendering
    // Add mathematical symbol display
    // Add math interaction support
}

// Minimal extensions to existing rendering system
class LaTeXRenderingExtensions {
public:
    // Extend existing renderers (no new pipeline)
    void extend_pdf_for_math(/* existing PDF renderer context */);
    void extend_svg_for_math(/* existing SVG renderer context */);
    void extend_screen_for_math(/* existing screen renderer context */);
    
    // Math-specific rendering helpers
    void render_math_symbols(ViewText* math_text, /* renderer context */);
    void render_math_layout(ViewSpan* math_span, /* renderer context */);
    void apply_math_fonts(ViewText* text, /* renderer context */);
    
    // Integration with existing font system
    void setup_math_fonts_in_existing_system(UiContext* ui_context);
};
```

### Phase 7: Testing and Validation (Week 7)
**Objective**: Comprehensive testing of the refactored system

#### 7.1 Test Infrastructure (GTest)
**Files**: `test/radiant_typeset/`

```cpp
// GTest-based Radiant typeset test suite
#include <gtest/gtest.h>
#include "radiant/typeset/typeset_engine.hpp"
#include "radiant/typeset/latex_bridge.hpp"
#include "radiant/typeset/math_layout.hpp"

class RadiantTypesetTest : public ::testing::Test {
protected:
    void SetUp() override {
        ui_context = create_test_ui_context();
        engine = new RadiantTypesetEngine(ui_context);
        bridge = new LaTeXRadiantBridge(engine);
    }
    
    void TearDown() override {
        delete bridge;
        delete engine;
        destroy_test_ui_context(ui_context);
    }
    
    UiContext* ui_context;
    RadiantTypesetEngine* engine;
    LaTeXRadiantBridge* bridge;
};

// Integration tests
TEST_F(RadiantTypesetTest, LaTeXToRadiantConversion) {
    // Test basic LaTeX document conversion
}

TEST_F(RadiantTypesetTest, MathLayoutAccuracy) {
    // Test mathematical expression layout
}

TEST_F(RadiantTypesetTest, DocumentStructureProcessing) {
    // Test document structure handling
}

TEST_F(RadiantTypesetTest, TableLayoutIntegration) {
    // Test table processing with existing ViewTable
}

// Math-specific tests
class MathLayoutTest : public ::testing::Test {
protected:
    void SetUp() override {
        ui_context = create_test_ui_context();
        math_engine = new MathLayoutEngine(ui_context);
    }
    
    void TearDown() override {
        delete math_engine;
        destroy_test_ui_context(ui_context);
    }
    
    UiContext* ui_context;
    MathLayoutEngine* math_engine;
};

TEST_F(MathLayoutTest, FractionLayout) {
    // Test fraction rendering using ViewSpan
}

TEST_F(MathLayoutTest, SuperscriptSubscript) {
    // Test superscript/subscript positioning
}

TEST_F(MathLayoutTest, MathSymbols) {
    // Test mathematical symbol rendering
}

// Performance tests
class RadiantTypesetPerformanceTest : public ::testing::Test {
protected:
    void SetUp() override {
        ui_context = create_test_ui_context();
        engine = new RadiantTypesetEngine(ui_context);
    }
    
    void TearDown() override {
        delete engine;
        destroy_test_ui_context(ui_context);
    }
    
    UiContext* ui_context;
    RadiantTypesetEngine* engine;
};

TEST_F(RadiantTypesetPerformanceTest, LargeDocumentProcessing) {
    // Test performance with large documents
}

TEST_F(RadiantTypesetPerformanceTest, ComplexMathPerformance) {
    // Test complex mathematical expression performance
}
```

#### 7.2 Quality Validation
**Test Documents**:
- Academic papers with complex math
- Technical reports with tables and figures
- Books with chapters and cross-references
- Mathematical documents with heavy notation
- Multi-language documents

**Validation Criteria**:
- Layout accuracy compared to reference LaTeX output
- Mathematical notation precision
- Font rendering quality
- Performance benchmarks
- Memory usage optimization

### Phase 8: Documentation and Finalization (Week 8)
**Objective**: Complete documentation and production readiness

#### 8.1 API Documentation
**Files**: `radiant/typeset/README.md`, API docs

```markdown
# Radiant LaTeX Typesetting System

## Overview
The Radiant LaTeX typesetting system provides high-quality LaTeX document processing
with production-ready rendering capabilities.

## Usage

### Command Line Interface
```bash
# Layout LaTeX document
./radiant.exe layout document.tex --output layout.json

# Render to PDF
./radiant.exe render document.tex --format pdf --output document.pdf

# Render to SVG
./radiant.exe render document.tex --format svg --output document.svg

# Render to PNG
./radiant.exe render document.tex --format png --output document.png --dpi 300
```

### Programmatic API
```cpp
#include "radiant/typeset/typeset_engine.hpp"

// Create typeset engine
RadiantTypesetEngine engine(ui_context);

// Process LaTeX document
Item latex_ast = parse_latex_file("document.tex");
ViewTree* tree = engine.typeset_latex_document(latex_ast, options);

// Render to various formats
render_to_pdf(tree, "output.pdf");
render_to_svg(tree, "output.svg");
render_to_png(tree, "output.png");
```
```

## Implementation Timeline

### 8-Week Development Schedule

```
Week 1: Analysis and Planning
- Architecture design
- Integration point analysis
- API specification

Week 2: Core Infrastructure
- Radiant typeset engine foundation
- Lambda bridge adapter
- Basic view conversion

Week 3: Mathematical Typesetting
- Math layout engine
- Mathematical symbols
- Math spacing and positioning

Week 4: Document Structure
- Document structure processor
- Text formatting engine
- Sectioning and numbering

Week 5: Advanced Features
- Table processing
- List processing
- Cross-references and citations

Week 6: Integration and CLI
- CLI enhancement
- Rendering pipeline integration
- Command-line interface

Week 7: Testing and Validation
- Test infrastructure
- Quality validation
- Performance optimization

Week 8: Documentation and Finalization
- API documentation
- User guides
- Production deployment

## Key Benefits of Refactoring

### 1. Unified View System
- **Before**: Separate typeset ViewTree with custom math views
- **After**: Single Radiant view system (ViewBlock/ViewSpan/ViewText) with minimal math extensions
- **Benefit**: Consistent view handling, reduced code duplication

### 2. Reuse Existing Infrastructure
- **Before**: Custom typeset rendering pipeline
- **After**: Extend existing Radiant renderers (render_pdf.cpp, render_svg.cpp, render.cpp)
- **Benefit**: Leverage proven rendering code, minimal new development

### 3. Parser Integration
- **Before**: Custom LaTeX parsing in typeset module
- **After**: Reuse existing input-latex.cpp and input-math.cpp
- **Benefit**: Consistent parsing, shared math processing capabilities

### 4. Simplified Architecture
- **Before**: Complex typeset module with separate view system
- **After**: Simple extensions to existing Radiant views and renderers
- **Benefit**: Easier maintenance, reduced architectural complexity

### 5. Minimal View Extensions
- **Before**: Complete custom ViewTree system
- **After**: Minimal ViewMath extension when absolutely necessary, reuse ViewSpan/ViewBlock/ViewText
- **Benefit**: Maximum code reuse, minimal new view types

## Migration Strategy

### Backward Compatibility
- Maintain existing Lambda LaTeX parser
- Keep existing typeset API for gradual migration
- Provide compatibility layer for existing code

### Gradual Rollout
1. **Phase 1**: New Radiant-based engine alongside existing system
2. **Phase 2**: Feature parity validation and testing
3. **Phase 3**: Default to new system with fallback option
4. **Phase 4**: Remove old typeset system after validation

### Risk Mitigation
- Comprehensive test suite comparing old vs new output
- Performance benchmarking to ensure no regressions
- Gradual feature migration to minimize disruption
- Rollback capability during transition period

## Success Criteria

### Functional Requirements
1. **Complete LaTeX Support**: All existing LaTeX features working
2. **Rendering Quality**: Output quality matches or exceeds current system
3. **Performance**: Processing speed equal or better than current system
4. **CLI Integration**: Seamless integration with Radiant CLI
5. **API Compatibility**: Existing code can migrate with minimal changes

### Quality Requirements
1. **Layout Accuracy**: 99%+ accuracy compared to reference LaTeX
2. **Mathematical Precision**: Perfect mathematical notation rendering
3. **Font Quality**: High-quality font rendering across all formats
4. **Memory Efficiency**: Optimized memory usage for large documents
5. **Cross-Platform**: Consistent behavior across macOS, Linux, Windows

### Integration Requirements
1. **Build System**: Seamless integration with existing Makefile
2. **Dependencies**: No new external dependencies beyond existing
3. **Documentation**: Complete API and user documentation
4. **Testing**: Comprehensive test coverage (>90%)
5. **Performance**: Sub-second processing for typical documents

## Conclusion

This revised refactoring plan transforms the Lambda typeset system into a minimal extension of Radiant's existing view and rendering system. By maximizing reuse of existing infrastructure and minimizing new view types, we achieve:

**Maximum Reuse Strategy**:
- Reuse existing ViewBlock, ViewSpan, ViewText for most LaTeX elements
- Reuse existing input-latex.cpp and input-math.cpp parsers
- Extend existing render_pdf.cpp, render_svg.cpp, render.cpp (no new pipeline)
- Add minimal ViewMath extension only when absolutely necessary

**Simplified Architecture**:
- Single directory: `radiant/typeset/` (no subdirectories)
- Minimal new files: 5 core files instead of complex module structure
- Direct integration with existing Radiant CLI commands
- No separate rendering pipeline or view system

**Implementation Benefits**:
- Reduced development time through maximum code reuse
- Lower maintenance burden with unified view system
- Consistent behavior across HTML and LaTeX processing
- Proven reliability through existing Radiant infrastructure

The 8-week timeline remains achievable with this simplified approach, focusing on extension rather than recreation of existing capabilities.
