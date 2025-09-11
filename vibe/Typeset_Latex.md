# LaTeX Typeset Enhancement Plan

## Overview

This document outlines the plan to enhance the Lambda typeset sub-module to support LaTeX input and PDF output generation. The approach focuses on creating new, separate LaTeX bridging files to minimize disruption to existing typesetting flows.

## Architecture

### Current Typeset System
- **TypesetEngine**: Core typesetting engine with Lambda context and options
- **ViewTree**: Device-independent representation of formatted content
- **Renderers**: Output-specific renderers (SVG, PDF, HTML)
- **Input parsers**: Handle various input formats through Lambda's input system

### New LaTeX Integration

#### 1. LaTeX Bridge (`typeset/integration/latex_bridge.h/c`)
- **Purpose**: Convert LaTeX AST to ViewTree representation
- **Key Function**: `create_view_tree_from_latex_ast()`
- **Responsibilities**:
  - Parse LaTeX document structure (sections, paragraphs, lists)
  - Extract and validate document metadata (title, author, etc.)
  - Handle LaTeX-specific elements (math, tables, figures, citations)
  - Map LaTeX formatting to ViewTree nodes

#### 2. LaTeX Typeset Entry Point (`typeset/typeset_latex.c`)
- **Purpose**: Standalone LaTeX typesetting functions 
- **Key Functions**:
  - `typeset_latex_to_view_tree()`: Main conversion function
  - `typeset_latex_to_pdf()`: Direct LaTeX → PDF pipeline
  - `typeset_latex_to_svg()`: LaTeX → SVG conversion
  - `typeset_latex_to_html()`: LaTeX → HTML conversion
  - `validate_latex_ast()`: Input validation
  - `fn_typeset_latex()`: Lambda function integration

#### 3. LaTeX Options Extension (`typeset/latex_typeset.h`)
- **Purpose**: LaTeX-specific configuration options
- **Structure**: `LatexTypesetOptions` extends base `TypesetOptions`
- **LaTeX Features**:
  - Citation processing (`\cite`, `\ref`)
  - Bibliography generation
  - Table of contents generation
  - Section/equation numbering
  - Math rendering (inline and display)
  - Custom fonts and styling

## Implementation Strategy

### Phase 1: Basic LaTeX Support ✅
- [x] Create LaTeX bridge files (`latex_bridge.h/c`)
- [x] Create LaTeX typeset entry point (`typeset_latex.c`)
- [x] Update build configuration to include new files
- [x] Implement basic LaTeX AST to ViewTree conversion
- [x] Add input validation and error handling

### Phase 2: Core LaTeX Features
- [ ] **Document Structure**: Sections, subsections, paragraphs
- [ ] **Math Rendering**: Inline `$...$` and display `$$...$$` math
- [ ] **Tables**: Basic table support with `tabular` environment
- [ ] **Lists**: Itemized and enumerated lists
- [ ] **Text Formatting**: Bold, italic, typewriter fonts

### Phase 3: Advanced LaTeX Features
- [ ] **Figures and Graphics**: `\includegraphics`, `figure` environment
- [ ] **Citations**: `\cite` command and bibliography
- [ ] **Cross-references**: `\ref`, `\label` system
- [ ] **Table of Contents**: Automatic TOC generation
- [ ] **Custom Commands**: Basic macro expansion

### Phase 4: Output Enhancement
- [ ] **PDF Renderer**: Integrate with existing PDF output system
- [ ] **Font Management**: LaTeX font selection (Computer Modern, etc.)
- [ ] **Page Layout**: Proper margins, headers, footers
- [ ] **Quality Settings**: DPI, compression, optimization

## File Structure

```
typeset/
├── typeset.h                    # Main typeset API (unchanged)
├── typeset.c                    # Main typeset implementation (unchanged)
├── typeset_latex.c              # New LaTeX-specific implementation ✅
├── latex_typeset.h              # LaTeX options and API ✅
├── integration/
│   ├── lambda_bridge.c          # Existing Lambda integration (unchanged)
│   └── latex_bridge.c           # New LaTeX AST bridge ✅
├── view/
│   └── view_tree.h              # ViewTree definitions (existing)
└── output/
    ├── svg_renderer.c           # SVG output (existing)
    ├── pdf_renderer.c           # PDF output (existing)
    └── html_renderer.c          # HTML output (existing)
```

## API Design

### Core LaTeX Functions

```c
// Main conversion function
ViewTree* typeset_latex_to_view_tree(TypesetEngine* engine, 
                                     Item latex_ast, 
                                     TypesetOptions* options);

// Direct output functions
bool typeset_latex_to_pdf(TypesetEngine* engine, Item latex_ast, 
                          const char* output_path, TypesetOptions* options);
bool typeset_latex_to_svg(TypesetEngine* engine, Item latex_ast, 
                          const char* output_path, TypesetOptions* options);

// Validation and preprocessing
bool validate_latex_ast(Item latex_ast);
Item preprocess_latex_ast(Item latex_ast);

// Lambda integration
Item fn_typeset_latex(Context* ctx, Item* args, int arg_count);
```

### Options Configuration

```c
typedef struct {
    TypesetOptions base;        // Base typeset options
    
    // LaTeX-specific settings
    bool process_citations;
    bool process_references;
    bool generate_toc;
    bool number_sections;
    bool render_math_inline;
    bool render_math_display;
    
    // Styling
    char* math_font;
    char* bibliography_style;
    double pdf_dpi;
} LatexTypesetOptions;
```

## Integration Points

### With Existing Lambda System
- **Input Parser**: Use existing `input-latex.c` for LaTeX parsing
- **Lambda AST**: Work with Lambda's Item/AST representation
- **Context**: Integrate with Lambda's execution context
- **Memory**: Use Lambda's memory pool system

### With Existing Typeset System
- **TypesetEngine**: Reuse existing engine structure
- **ViewTree**: Target existing ViewTree representation
- **Renderers**: Use existing PDF/SVG/HTML renderers
- **Options**: Extend existing TypesetOptions

## Testing Strategy

### Unit Tests
- LaTeX AST validation
- ViewTree conversion accuracy
- Options parsing and application
- Error handling and edge cases

### Integration Tests
- End-to-end LaTeX → PDF conversion
- Comparison with reference LaTeX processors
- Performance benchmarking
- Memory usage validation

### Test Cases
1. **Basic Document**: Simple LaTeX with text and formatting
2. **Math Document**: LaTeX with inline and display math
3. **Structured Document**: Sections, lists, tables
4. **Complex Document**: Figures, citations, cross-references

## Benefits

### For Users
- **Direct LaTeX Support**: Process LaTeX documents without external tools
- **Multiple Outputs**: Generate PDF, SVG, HTML from same LaTeX source
- **Integration**: Use LaTeX within Lambda script workflows
- **Performance**: Fast processing with JIT compilation

### For Developers
- **Modular Design**: New features isolated from existing code
- **Extensible**: Easy to add new LaTeX features
- **Maintainable**: Clear separation of concerns
- **Testable**: Individual components can be tested independently

## Current Status

- ✅ **Architecture Planned**: Clear separation of new LaTeX features
- ✅ **Files Created**: Basic LaTeX bridge and entry point files
- ✅ **Build Integration**: New files included in build system
- 🔄 **Implementation**: Basic structure implemented, needs feature completion
- ❌ **Testing**: Test suite needs to be developed
- ❌ **Documentation**: User documentation needs to be written

## Next Steps

1. **Fix Build Issues**: Ensure clean compilation of new files
2. **Implement Core Features**: Basic LaTeX document structure support
3. **Add Math Rendering**: Inline and display math processing
4. **Create Test Suite**: Unit and integration tests
5. **Performance Optimization**: Benchmarking and optimization
6. **User Documentation**: API reference and usage examples

This modular approach ensures that LaTeX support can be developed and tested independently while maintaining compatibility with the existing typeset system.
- **Build Integration**: Makefile targets for LaTeX typesetting tests

## Phase 1: LaTeX Input Processing Enhancement

### 1.1 LaTeX Parser Integration Analysis

**Current Capabilities** (from `input-latex.cpp`):
- Document structure parsing (sections, subsections, chapters)
- Text formatting (bold, italic, underline, font sizes)
- Mathematical expressions (inline and display math)
- Lists (itemize, enumerate, description)
- Tables (tabular environment)
- Cross-references and citations
- Package imports and commands
- Special characters and escaping

**Required Enhancements**:
- Enhanced mathematical layout support
- Advanced table layouts
- Figure and float positioning
- Bibliography and citation handling
- Custom command expansion

### 1.2 Lambda AST to ViewTree Mapping

**File**: `typeset/integration/lambda_bridge.c` (333 lines, functional)

**Required Extensions**:
```c
// New LaTeX-specific view node types
typedef enum {
    VIEW_NODE_LATEX_SECTION,     // \section, \subsection
    VIEW_NODE_LATEX_MATH_BLOCK,  // \begin{equation}
    VIEW_NODE_LATEX_TABLE,       // \begin{tabular}
    VIEW_NODE_LATEX_FIGURE,      // \begin{figure}
    VIEW_NODE_LATEX_LIST,        // \begin{itemize}
    VIEW_NODE_LATEX_VERBATIM,    // \begin{verbatim}
    VIEW_NODE_LATEX_ABSTRACT,    // \begin{abstract}
    VIEW_NODE_LATEX_TOC          // \tableofcontents
} ViewNodeLatexType;

// Enhanced LaTeX mapping functions
ViewNode* map_latex_document_to_view(Element* latex_doc, ViewTree* tree);
ViewNode* map_latex_section_to_view(Element* section, ViewTree* tree);
ViewNode* map_latex_math_to_view(Element* math, ViewTree* tree);
ViewNode* map_latex_table_to_view(Element* table, ViewTree* tree);
```

**Implementation Strategy**:
1. Extend existing `lambda_element_tree_to_view_tree()` function
2. Add LaTeX-specific element recognition
3. Implement LaTeX formatting rules (font sizes, spacing, margins)
4. Handle LaTeX-specific mathematical notation
5. Process LaTeX document structure (title, abstract, sections)

### 1.3 Enhanced Mathematical Layout

**File**: `typeset/math_typeset.c` (existing but needs LaTeX support)

**LaTeX Math Features to Support**:
- Display equations with numbering
- Equation arrays and alignments
- Mathematical symbols and operators
- Subscripts and superscripts
- Fractions and radicals
- Matrices and arrays
- Mathematical fonts (mathcal, mathbb, mathfrak)

## Phase 2: PDF Renderer Implementation

### 2.1 PDF Renderer Architecture

**New File**: `typeset/output/pdf_renderer.c`

**Dependencies**:
- **libharu** (recommended) or **cairo-pdf** for PDF generation
- Font embedding and management
- Vector graphics and text rendering

**Core Functions**:
```c
// PDF renderer interface
typedef struct PDFRenderer {
    ViewRenderer base;
    PDF* pdf_document;          // libharu PDF handle
    HPDF_Page current_page;     // Current page
    FontManager* font_manager;   // Font management
    PDFRenderOptions* options;   // Rendering options
} PDFRenderer;

// Main rendering functions
PDFRenderer* pdf_renderer_create(PDFRenderOptions* options);
bool pdf_render_view_tree(PDFRenderer* renderer, ViewTree* tree);
bool pdf_save_to_file(PDFRenderer* renderer, const char* filename);
void pdf_renderer_destroy(PDFRenderer* renderer);

// Page and layout functions
void pdf_start_page(PDFRenderer* renderer, double width, double height);
void pdf_render_text(PDFRenderer* renderer, ViewTextRun* text);
void pdf_render_math(PDFRenderer* renderer, ViewMathElement* math);
void pdf_render_geometry(PDFRenderer* renderer, ViewGeometry* geom);
```

### 2.2 PDF Rendering Features

**Typography**:
- Font embedding (Type1, TrueType, OpenType)
- Unicode text support with proper glyph shaping
- Text metrics and baseline alignment
- Font size and style management

**Layout**:
- Multi-page documents
- Page margins and headers/footers
- Table of contents with bookmarks
- Cross-references and hyperlinks

**Graphics**:
- Vector graphics (lines, rectangles, circles)
- Mathematical symbols and equations
- Image embedding (PNG, JPEG)
- Color management (RGB, CMYK)

### 2.3 Build System Integration

**Makefile Enhancements**:
```makefile
# PDF dependencies
PDF_LIBS = -lhpdf -lpng -lz

# Add PDF renderer to typeset sources
TYPESET_SOURCES += $(TYPESET_DIR)/output/pdf_renderer.c

# New targets
test-typeset-latex: $(BUILD_DIR)/test_latex_typeset
	./$(BUILD_DIR)/test_latex_typeset

test-latex-pdf: test-typeset-latex
	@echo "Running LaTeX to PDF typesetting tests..."
	./$(BUILD_DIR)/test_latex_typeset --output-pdf

validate-latex-pdf: test-latex-pdf
	@echo "Validating generated PDFs against reference..."
	./test/validate_latex_pdfs.sh
```

## Phase 3: Comprehensive Test Suite

### 3.1 Curated LaTeX Test Documents

**Test Categories**:

1. **Basic Structure** (`test/input/latex/basic_structure.tex`):
   ```latex
   \documentclass{article}
   \title{Basic Structure Test}
   \author{Test}
   \begin{document}
   \maketitle
   \section{Introduction}
   \subsection{Overview}
   Basic text with \textbf{bold} and \textit{italic}.
   \end{document}
   ```

2. **Mathematical Content** (`test/input/latex/math_comprehensive.tex`):
   ```latex
   \documentclass{article}
   \usepackage{amsmath,amsfonts}
   \begin{document}
   \section{Mathematics}
   Inline math: $E = mc^2$
   
   Display equation:
   \begin{equation}
   \int_{-\infty}^{\infty} e^{-x^2} dx = \sqrt{\pi}
   \end{equation}
   
   Matrix:
   \begin{pmatrix}
   a & b \\
   c & d
   \end{pmatrix}
   \end{document}
   ```

3. **Complex Layout** (`test/input/latex/complex_layout.tex`):
   ```latex
   \documentclass[twocolumn]{article}
   \usepackage{graphicx,array}
   \begin{document}
   \begin{table}
   \centering
   \begin{tabular}{|c|c|c|}
   \hline
   Header 1 & Header 2 & Header 3 \\
   \hline
   Data 1 & Data 2 & Data 3 \\
   \hline
   \end{tabular}
   \caption{Test Table}
   \end{table}
   \end{document}
   ```

4. **Academic Paper** (`test/input/latex/academic_paper.tex`):
   ```latex
   \documentclass[12pt,a4paper]{article}
   \usepackage[utf8]{inputenc}
   \usepackage{amsmath,graphicx,cite}
   \title{Academic Paper Template}
   \author{Author Name}
   \begin{document}
   \maketitle
   \begin{abstract}
   This is an abstract.
   \end{abstract}
   \tableofcontents
   \section{Introduction}
   \cite{reference1}
   \bibliography{references}
   \end{document}
   ```

### 3.2 Reference PDF Generation

**Script**: `test/generate_reference_pdfs.sh`
```bash
#!/bin/bash
# Generate reference PDFs using pdflatex

LATEX_DIR="test/input/latex"
REF_DIR="test/reference/pdf"
mkdir -p "$REF_DIR"

for tex_file in "$LATEX_DIR"/*.tex; do
    filename=$(basename "$tex_file" .tex)
    echo "Generating reference PDF for $filename..."
    
    # Generate PDF with pdflatex
    cd "$(dirname "$tex_file")" || exit 1
    pdflatex -output-directory="$REF_DIR" "$tex_file"
    pdflatex -output-directory="$REF_DIR" "$tex_file"  # Second pass for references
    
    # Clean auxiliary files
    rm -f "$REF_DIR"/*.aux "$REF_DIR"/*.log "$REF_DIR"/*.toc
done

echo "Reference PDFs generated in $REF_DIR"
```

### 3.3 PDF Comparison and Validation

**Tools Installation**:
```bash
# Install PDF comparison tools
brew install diff-pdf          # Visual PDF comparison
```

**Validation Script**: `test/validate_latex_pdfs.sh`
```bash
#!/bin/bash
# Compare generated PDFs with reference PDFs

TEST_DIR="test/output/pdf"
REF_DIR="test/reference/pdf"
DIFF_DIR="test/output/diff"
mkdir -p "$DIFF_DIR"

success_count=0
total_count=0

for ref_pdf in "$REF_DIR"/*.pdf; do
    filename=$(basename "$ref_pdf")
    test_pdf="$TEST_DIR/$filename"
    diff_output="$DIFF_DIR/${filename%.pdf}_diff.pdf"
    
    total_count=$((total_count + 1))
    
    if [[ ! -f "$test_pdf" ]]; then
        echo "❌ MISSING: $filename"
        continue
    fi
    
    echo "📊 Comparing $filename..."
    
    # Visual comparison with diff-pdf
    if diff-pdf --output-diff="$diff_output" "$ref_pdf" "$test_pdf" 2>/dev/null; then
        echo "✅ PASS: $filename (identical)"
        success_count=$((success_count + 1))
    else
        echo "❌ FAIL: $filename (differences found)"
        echo "   Diff saved to: $diff_output"
    fi
done

echo ""
echo "📈 Results: $success_count/$total_count tests passed"
if [[ $success_count -eq $total_count ]]; then
    echo "🎉 All PDF validation tests passed!"
    exit 0
else
    echo "⚠️  Some PDF validation tests failed"
    exit 1
fi
```

### 3.4 Automated Test Integration

**C Test File**: `test/test_latex_typeset.c`
```c
#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include "../typeset/typeset.h"
#include "../typeset/output/pdf_renderer.h"
#include "../lambda/input/input.h"
#include <stdio.h>
#include <stdlib.h>

Test(latex_typeset, basic_structure) {
    // Load LaTeX document
    Pool* pool = pool_init(1024 * 1024);
    Item latex_doc = input_auto_detect(pool, "test/input/latex/basic_structure.tex");
    cr_assert_eq(get_type_id(latex_doc), LMD_TYPE_ELEMENT);
    
    // Convert to view tree
    ViewTree* tree = lambda_element_tree_to_view_tree((Element*)latex_doc.pointer);
    cr_assert_not_null(tree);
    cr_assert_gt(tree->page_count, 0);
    
    // Render to PDF
    PDFRenderOptions options = {0};
    PDFRenderer* renderer = pdf_renderer_create(&options);
    cr_assert_not_null(renderer);
    
    bool success = pdf_render_view_tree(renderer, tree);
    cr_assert(success);
    
    success = pdf_save_to_file(renderer, "test/output/pdf/basic_structure.pdf");
    cr_assert(success);
    
    // Cleanup
    pdf_renderer_destroy(renderer);
    view_tree_destroy(tree);
    pool_cleanup(pool);
}

Test(latex_typeset, mathematical_content) {
    // Similar structure for math-heavy documents
    Pool* pool = pool_init(1024 * 1024);
    Item latex_doc = input_auto_detect(pool, "test/input/latex/math_comprehensive.tex");
    
    // Test mathematical element conversion
    ViewTree* tree = lambda_element_tree_to_view_tree((Element*)latex_doc.pointer);
    cr_assert_not_null(tree);
    
    // Verify math elements are properly converted
    int math_count = count_view_nodes_by_type(tree, VIEW_NODE_LATEX_MATH_BLOCK);
    cr_assert_gt(math_count, 0);
    
    // Render and validate
    PDFRenderer* renderer = pdf_renderer_create(NULL);
    pdf_render_view_tree(renderer, tree);
    pdf_save_to_file(renderer, "test/output/pdf/math_comprehensive.pdf");
    
    pdf_renderer_destroy(renderer);
    view_tree_destroy(tree);
    pool_cleanup(pool);
}
```

## Phase 4: Implementation Timeline

### Week 1: PDF Renderer Foundation
- [ ] Install and configure libharu dependency
- [ ] Implement basic PDF renderer structure
- [ ] Create PDFRenderer class with core functions
- [ ] Basic text rendering and page management
- [ ] Integration with existing renderer interface

### Week 2: LaTeX-Specific Enhancements
- [ ] Extend lambda_bridge.c for LaTeX elements
- [ ] Implement LaTeX document structure mapping
- [ ] Add LaTeX mathematical layout support
- [ ] Enhanced font and typography handling
- [ ] LaTeX-specific styling and formatting

### Week 3: Test Suite Development
- [ ] Create curated LaTeX test documents
- [ ] Implement reference PDF generation scripts
- [ ] Set up PDF comparison and validation tools
- [ ] Write comprehensive C test cases
- [ ] Integrate with existing test framework

### Week 4: Integration and Validation
- [ ] Complete end-to-end LaTeX-to-PDF pipeline
- [ ] Performance optimization and memory management
- [ ] Documentation and user guides
- [ ] Final validation and quality assurance
- [ ] Integration with main Lambda build system

## Success Criteria

### ✅ Functional Requirements
1. **LaTeX Input Processing**: Parse complex LaTeX documents correctly
2. **View Tree Generation**: Convert LaTeX AST to device-independent view tree
3. **PDF Rendering**: Generate professional-quality PDF output
4. **Mathematical Layout**: Properly render LaTeX mathematical expressions
5. **Test Validation**: All curated test cases pass PDF comparison

### ✅ Quality Requirements
1. **Performance**: Process typical LaTeX documents under 1 second
2. **Memory Safety**: No memory leaks in typeset pipeline
3. **Compatibility**: Support major LaTeX document classes and packages
4. **Accuracy**: Generated PDFs match reference output within 95% similarity
5. **Maintainability**: Clean, documented code following project standards

### ✅ Integration Requirements
1. **Build System**: Seamless integration with existing Makefile
2. **Dependencies**: Minimal external dependencies, well-documented
3. **API Consistency**: Follows existing Lambda typeset API patterns
4. **Error Handling**: Graceful fallbacks and proper error reporting
5. **Cross-Platform**: Works on macOS, Linux, and Windows

## Dependencies and Prerequisites

### External Libraries
```bash
# Install required dependencies
brew install libharu              # PDF generation
brew install diff-pdf             # PDF comparison

# Verify TeX distribution
which pdflatex                   # Should be available
which bibtex                     # For bibliography support
```

### Build Configuration
```json
// build_lambda_config.json additions
{
  "typeset": {
    "pdf_renderer": true,
    "latex_support": true,
    "external_libs": ["hpdf", "png", "z"]
  }
}
```

## Risk Assessment and Mitigation

### High Risk: PDF Library Integration
- **Risk**: libharu integration complexity
- **Mitigation**: Start with simple PDF generation, gradually add features
- **Fallback**: Use cairo-pdf as alternative backend

### Medium Risk: LaTeX Complexity
- **Risk**: LaTeX parser may not handle all constructs
- **Mitigation**: Focus on core LaTeX features, document limitations
- **Enhancement**: Incremental support for advanced features

### Low Risk: Test Suite Maintenance
- **Risk**: Reference PDFs may become outdated
- **Mitigation**: Automated regeneration scripts, version control for references
- **Process**: Regular validation against multiple LaTeX engines

## Future Enhancements

### Advanced Features (Post-Phase 4)
1. **Custom LaTeX Packages**: Support for specialized packages
2. **Interactive PDFs**: Hyperlinks, bookmarks, forms
3. **Multi-Language Support**: Unicode and RTL text rendering
4. **Performance Optimization**: Caching and incremental rendering
5. **Visual Editor Integration**: Real-time preview capabilities

### Integration Opportunities
1. **Web Interface**: Browser-based LaTeX editing and preview
2. **API Endpoints**: REST API for LaTeX-to-PDF conversion
3. **Plugin Architecture**: Extensible renderer system
4. **Cloud Deployment**: Scalable document processing service

## Conclusion

This comprehensive plan provides a roadmap for enhancing Lambda typeset to support LaTeX input and PDF output, creating a robust, tested, and professional document processing pipeline. The modular approach ensures maintainability while the comprehensive test suite guarantees quality and reliability.

The implementation leverages existing Lambda infrastructure while adding targeted enhancements for LaTeX-specific requirements, resulting in a powerful and versatile typesetting system suitable for academic, technical, and professional document production.
