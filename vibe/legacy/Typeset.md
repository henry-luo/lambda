# Typeset Layout Engine Implementation Plan - Phase 2

## Overview

This document outlines the incremental implementation plan for **Phase 2: Layout Engine** of the Lambda Typesetting System. Based on analysis of the existing codebase and the foundational architecture already implemented, this phase focuses on implementing the core layout algorithms: text layout with line breaking and font metrics, CSS-like box model, and multi-page pagination.

## Current State Analysis

### Existing Foundation ✅
- **View Tree System**: Complete data structures in `typeset/view/view_tree.h`
- **Basic Text Nodes**: `ViewTextRun` structure with glyph positioning
- **Coordinate System**: Device-independent points (1/72 inch)
- **Node Hierarchy**: Parent-child relationships and traversal
- **Transform System**: 2D transformation matrices

### Architecture Leverage
The implementation will leverage existing Lambda systems:
- **Radiant Layout Engine**: Study `radiant/layout*.c` for text flow algorithms
- **Font System**: Existing font loading and metrics from Radiant
- **Memory Pools**: Use Lambda's pool allocation for performance
- **Unicode Support**: Lambda's ICU integration for text processing

## Phase 2 Implementation Plan

### 2.1 Text Layout Engine (4-6 weeks)

#### 2.1.1 Font System Integration (Week 1)
**Goal**: Integrate font loading and metrics into the typeset system

**Files to Create/Modify**:
```
typeset/font/
├── font_manager.h         # Font management interface  
├── font_manager.c         # Font loading and caching
├── font_metrics.h         # Font metrics and measurements
├── font_metrics.c         # Glyph measurements and positioning
└── text_shaper.h          # Text shaping interface
└── text_shaper.c          # Unicode text shaping
```

**Implementation Steps**:
1. **Font Manager**: Create font loading system using existing Lambda font code
   ```c
   typedef struct FontManager {
       FontCache* font_cache;       // Cache loaded fonts
       Context* lambda_context;     // Lambda memory context
       char* default_font_family;   // Default font
       double default_font_size;    // Default size
   } FontManager;
   
   FontManager* font_manager_create(Context* ctx);
   ViewFont* font_manager_get_font(FontManager* mgr, const char* family, double size, int weight);
   ```

2. **Font Metrics**: Implement precise glyph measurements
   ```c
   typedef struct FontMetrics {
       double ascent;              // Font ascent
       double descent;             // Font descent  
       double line_height;         // Default line height
       double x_height;            // x-height for alignment
       double cap_height;          // Capital letter height
       double advance_width;       // Average character width
   } FontMetrics;
   
   FontMetrics font_get_metrics(ViewFont* font);
   double font_measure_text_width(ViewFont* font, const char* text, int length);
   ```

3. **Text Shaping**: Unicode-aware text processing using ICU
   ```c
   typedef struct TextShapeResult {
       ViewGlyphInfo* glyphs;      // Shaped glyphs
       ViewPoint* positions;       // Glyph positions
       int glyph_count;            // Number of glyphs
       double total_width;         // Total text width
   } TextShapeResult;
   
   TextShapeResult* text_shape(ViewFont* font, const char* text, int length);
   ```

#### 2.1.2 Line Breaking Algorithm (Week 2)
**Goal**: Implement sophisticated line breaking with hyphenation

**Files to Create**:
```
typeset/layout/
├── line_breaker.h         # Line breaking interface
├── line_breaker.c         # Line breaking implementation  
├── word_wrap.h            # Word wrapping utilities
└── word_wrap.c            # Word boundary detection
```

**Implementation Steps**:
1. **Line Breaking Context**: Track line state during text flow
   ```c
   typedef struct LineBreakContext {
       double line_width;          // Available line width
       double current_width;       // Current line width used
       ViewFont* current_font;     // Current font
       
       // Break opportunities
       struct BreakPoint {
           int text_position;      // Position in text
           double width_before;    // Width before break
           double penalty;         // Break penalty (0 = good break)
           bool is_hyphen;         // Hyphenated break
       }* break_points;
       int break_point_count;
       
       // Line state
       bool allow_hyphenation;     // Enable hyphenation
       double hyphen_penalty;      // Penalty for hyphens
       double widow_penalty;       // Penalty for widows
   } LineBreakContext;
   ```

2. **Break Point Detection**: Find optimal break points using Knuth-Plass algorithm
   ```c
   BreakPointList* find_line_breaks(LineBreakContext* ctx, const char* text, int length);
   bool is_break_opportunity(const char* text, int position);
   double calculate_break_penalty(LineBreakContext* ctx, int position, bool is_hyphen);
   ```

3. **Hyphenation**: Language-aware hyphenation using patterns
   ```c
   typedef struct HyphenationDict {
       char* language;             // Language code (e.g., "en-US")
       void* pattern_data;         // Hyphenation patterns
   } HyphenationDict;
   
   HyphenationDict* load_hyphenation_dict(const char* language);
   char* hyphenate_word(HyphenationDict* dict, const char* word);
   ```

#### 2.1.3 Text Flow Engine (Week 3)
**Goal**: Implement text flow with proper spacing and alignment

**Files to Create**:
```
typeset/layout/
├── text_flow.h            # Text flow interface
├── text_flow.c            # Text flow implementation
├── spacing.h              # Text spacing calculations
└── spacing.c              # Inter-word and inter-character spacing
```

**Implementation Steps**:
1. **Text Flow Context**: Manage text flowing across lines
   ```c
   typedef struct TextFlowContext {
       ViewRect content_area;      // Available content area
       double current_x;           // Current X position
       double current_y;           // Current Y position  
       double line_height;         // Current line height
       
       ViewNode* current_line;     // Current line container
       ViewNode* current_paragraph; // Current paragraph
       
       // Alignment
       ViewTextAlign text_align;   // Text alignment (left, center, right, justify)
       double word_spacing;        // Extra word spacing
       double letter_spacing;      // Extra letter spacing
       
       // Paragraph settings
       double paragraph_indent;    // First line indent
       double paragraph_spacing;   // Space between paragraphs
   } TextFlowContext;
   ```

2. **Line Layout**: Position text runs within lines
   ```c
   ViewNode* create_text_line(TextFlowContext* ctx);
   void add_text_run_to_line(ViewNode* line, ViewNode* text_run);
   void finalize_line_layout(TextFlowContext* ctx, ViewNode* line);
   void apply_text_alignment(ViewNode* line, ViewTextAlign align, double available_width);
   ```

3. **Justification**: Full justification with space distribution
   ```c
   void justify_line(ViewNode* line, double target_width);
   double calculate_word_spacing_adjustment(ViewNode* line, double extra_space);
   void distribute_extra_space(ViewNode* line, double word_spacing, double letter_spacing);
   ```

#### 2.1.4 Vertical Metrics and Baseline (Week 4)
**Goal**: Implement proper vertical alignment and baseline handling

**Implementation Steps**:
1. **Baseline Calculation**: Align text runs on common baseline
   ```c
   typedef struct BaselineInfo {
       double alphabetic;          // Alphabetic baseline
       double ideographic;         // Ideographic baseline
       double hanging;             // Hanging baseline
       double mathematical;        // Mathematical baseline
   } BaselineInfo;
   
   BaselineInfo calculate_baseline_info(ViewFont* font);
   void align_text_runs_on_baseline(ViewNode* line);
   ```

2. **Vertical Metrics**: Handle mixed fonts and sizes in same line
   ```c
   typedef struct LineMetrics {
       double ascent;              // Maximum ascent in line
       double descent;             // Maximum descent in line  
       double line_height;         // Total line height
       double baseline_offset;     // Baseline position from top
   } LineMetrics;
   
   LineMetrics calculate_line_metrics(ViewNode* line);
   void apply_vertical_alignment(ViewNode* line, ViewVerticalAlign align);
   ```

### 2.2 Box Model Implementation (3-4 weeks)

#### 2.2.1 Box Model Core (Week 5)
**Goal**: Implement CSS-like box model with margins, borders, padding

**Files to Create**:
```
typeset/layout/
├── box_model.h            # Box model interface
├── box_model.c            # Box model implementation
├── margin_collapse.h      # Margin collapsing rules
└── margin_collapse.c      # Margin collapsing implementation
```

**Implementation Steps**:
1. **Box Model Structure**: Define CSS-like box properties
   ```c
   typedef struct ViewBox {
       // Content area
       ViewRect content_area;
       
       // Padding (inside border)
       struct {
           double top, right, bottom, left;
       } padding;
       
       // Border
       struct {
           double top, right, bottom, left;
           struct ViewColor color;
           enum BorderStyle {
               BORDER_NONE, BORDER_SOLID, BORDER_DASHED, BORDER_DOTTED
           } style;
       } border;
       
       // Margin (outside border)
       struct {
           double top, right, bottom, left;
           bool auto_top, auto_right, auto_bottom, auto_left;
       } margin;
       
       // Box sizing
       enum BoxSizing {
           BOX_SIZING_CONTENT,     // width = content width
           BOX_SIZING_BORDER       // width = content + padding + border
       } box_sizing;
   } ViewBox;
   ```

2. **Box Calculation**: Calculate actual box dimensions
   ```c
   ViewRect calculate_content_area(ViewBox* box, ViewSize available_size);
   ViewRect calculate_border_area(ViewBox* box);
   ViewRect calculate_margin_area(ViewBox* box);
   double calculate_total_width(ViewBox* box);
   double calculate_total_height(ViewBox* box);
   ```

3. **Margin Collapsing**: Implement CSS margin collapsing rules
   ```c
   double collapse_vertical_margins(double margin1, double margin2);
   void apply_margin_collapsing(ViewNode* parent, ViewNode* child);
   ```

#### 2.2.2 Block Layout Engine (Week 6)
**Goal**: Implement block-level element layout

**Files to Create**:
```
typeset/layout/
├── block_layout.h         # Block layout interface
├── block_layout.c         # Block layout implementation
├── flow_layout.h          # Normal flow layout
└── flow_layout.c          # Normal flow implementation
```

**Implementation Steps**:
1. **Block Layout Context**: Manage block-level layout
   ```c
   typedef struct BlockLayoutContext {
       ViewRect containing_block;  // Containing block dimensions
       double current_y;           // Current Y position
       double max_width;           // Maximum width used
       
       ViewNode* current_container; // Current container
       ViewBox* container_box;     // Container box model
       
       // Flow state
       bool in_normal_flow;        // Normal vs. positioned flow
       ViewNode* float_left_list;  // Left-floated elements
       ViewNode* float_right_list; // Right-floated elements
   } BlockLayoutContext;
   ```

2. **Block Positioning**: Position blocks vertically
   ```c
   void layout_block_element(BlockLayoutContext* ctx, ViewNode* block);
   void position_block_vertically(BlockLayoutContext* ctx, ViewNode* block);
   void calculate_block_width(ViewNode* block, double containing_width);
   void calculate_block_height(ViewNode* block);
   ```

#### 2.2.3 Inline Layout Engine (Week 7-8)  
**Goal**: Implement inline-level element layout

**Implementation Steps**:
1. **Inline Layout Context**: Manage inline flow
   ```c
   typedef struct InlineLayoutContext {
       ViewRect line_box;          // Current line box
       double baseline_y;          // Baseline position
       
       // Current position
       double current_x;           // Current X position
       ViewFont* current_font;     // Current font
       
       // Line state
       bool line_has_content;      // Line has content
       double line_ascent;         // Line ascent
       double line_descent;        // Line descent
   } InlineLayoutContext;
   ```

2. **Inline Positioning**: Position inline elements
   ```c
   void layout_inline_element(InlineLayoutContext* ctx, ViewNode* inline_elem);
   void position_inline_text(InlineLayoutContext* ctx, ViewNode* text_run);
   void handle_inline_break(InlineLayoutContext* ctx);
   ```

### 2.3 Pagination System (2-3 weeks)

#### 2.3.1 Page Breaking Engine (Week 9)
**Goal**: Implement multi-page layout with intelligent page breaks

**Files to Create**:
```
typeset/layout/
├── pagination.h           # Pagination interface
├── pagination.c           # Pagination implementation
├── page_breaks.h          # Page break detection
└── page_breaks.c          # Page break algorithms
```

**Implementation Steps**:
1. **Page Context**: Manage page layout
   ```c
   typedef struct PageContext {
       ViewSize page_size;         // Page dimensions
       ViewRect content_area;      // Content area (minus margins)
       ViewRect margin_area;       // Margin area
       
       int current_page;           // Current page number
       double current_y;           // Current Y position on page
       double remaining_height;    // Remaining height on page
       
       ViewNode* current_page_node; // Current page node
       ViewNode* page_content;     // Current page content container
   } PageContext;
   ```

2. **Page Break Detection**: Find optimal page break points
   ```c
   typedef enum PageBreakQuality {
       PAGE_BREAK_POOR = 0,        // Avoid this break
       PAGE_BREAK_NEUTRAL = 50,    // Acceptable break
       PAGE_BREAK_GOOD = 100       // Prefer this break
   } PageBreakQuality;
   
   PageBreakQuality evaluate_page_break(ViewNode* node, double y_position);
   ViewNode* find_best_page_break(ViewNode* content, double max_height);
   ```

3. **Page Break Handling**: Split content across pages
   ```c
   typedef struct PageBreakResult {
       ViewNode* before_break;     // Content before break
       ViewNode* after_break;      // Content after break
       bool is_split;              // Content was split
   } PageBreakResult;
   
   PageBreakResult* split_content_at_page_break(ViewNode* content, ViewNode* break_point);
   void move_content_to_next_page(PageContext* ctx, ViewNode* content);
   ```

#### 2.3.2 Page Layout Finalization (Week 10)
**Goal**: Finalize page layout with headers, footers, and page numbers

**Implementation Steps**:
1. **Page Elements**: Handle headers, footers, page numbers
   ```c
   typedef struct PageElements {
       ViewNode* header;           // Page header
       ViewNode* footer;           // Page footer
       ViewNode* page_number;      // Page number
       ViewNode* margin_notes;     // Margin notes
   } PageElements;
   
   void layout_page_elements(PageContext* ctx, PageElements* elements);
   void position_page_header(PageContext* ctx, ViewNode* header);
   void position_page_footer(PageContext* ctx, ViewNode* footer);
   ```

## Integration Plan

### 2.4 Layout Engine Integration (Week 11)

#### 2.4.1 Layout Algorithm Orchestration
**Goal**: Integrate all layout components into main layout engine

**Files to Modify**:
```
typeset/typeset.h          # Add layout engine APIs
typeset/typeset.c          # Integrate layout engine
typeset/view/view_tree.c   # Add layout support to view tree
```

**Implementation Steps**:
1. **Main Layout Function**: Orchestrate layout process
   ```c
   typedef struct LayoutEngine {
       FontManager* font_manager;
       BlockLayoutContext* block_context;
       InlineLayoutContext* inline_context;
       TextFlowContext* text_context;
       PageContext* page_context;
   } LayoutEngine;
   
   LayoutEngine* layout_engine_create(TypesetEngine* typeset_engine);
   ViewTree* layout_view_tree(LayoutEngine* engine, ViewTree* input_tree);
   ```

2. **Layout Process**: Define layout algorithm flow
   ```c
   void layout_document(LayoutEngine* engine, ViewTree* tree) {
       // Phase 1: Calculate intrinsic sizes
       calculate_intrinsic_sizes(tree->root);
       
       // Phase 2: Perform layout
       layout_block_flow(engine->block_context, tree->root);
       
       // Phase 3: Apply pagination
       apply_pagination(engine->page_context, tree);
       
       // Phase 4: Finalize positions
       finalize_layout(tree);
   }
   ```

#### 2.4.2 Lambda Integration Update
**Goal**: Update Lambda bridge to use new layout engine

**Files to Modify**:
```
typeset/integration/lambda_bridge.h
typeset/integration/lambda_bridge.c
```

**Implementation Steps**:
1. Update `create_view_tree_from_lambda_item()` to apply layout
2. Add layout options to conversion process
3. Integrate with existing SVG renderer

### 2.5 Testing and Validation (Week 12)

#### 2.5.1 Unit Tests
**Create test files**:
```
test/lambda/typeset/layout/
├── test_font_metrics.ls       # Font metrics tests
├── test_line_breaking.ls      # Line breaking tests  
├── test_text_flow.ls          # Text flow tests
├── test_box_model.ls          # Box model tests
├── test_pagination.ls         # Pagination tests
└── test_layout_integration.ls # Integration tests
```

#### 2.5.2 Performance Tests
1. **Memory Usage**: Test memory pool efficiency
2. **Layout Speed**: Benchmark layout performance
3. **Text Shaping**: Test Unicode text handling

## Implementation Timeline

### Weeks 1-4: Text Layout Foundation ✅
- **Week 1**: Font system integration ✅
- **Week 2**: Line breaking algorithm ✅
- **Week 3**: Text flow engine ✅
- **Week 4**: Vertical metrics and baseline ✅

### Weeks 5-8: Box Model Implementation
- **Week 5**: Box model core
- **Week 6**: Block layout engine
- **Week 7-8**: Inline layout engine

### Weeks 9-10: Pagination System
- **Week 9**: Page breaking engine
- **Week 10**: Page layout finalization

### Weeks 11-12: Integration and Testing
- **Week 11**: Layout engine integration
- **Week 12**: Testing and validation

## Success Criteria

### Functional Requirements
1. **Text Layout**:
   - ✅ Accurate font metrics and glyph positioning
   - ✅ Proper line breaking with hyphenation
   - ✅ Text alignment (left, right, center, justify)
   - ✅ Multi-font line handling

2. **Box Model**:
   - ✅ CSS-like margin, border, padding
   - ✅ Margin collapsing
   - ✅ Block and inline layout
   - ✅ Proper sizing calculations

3. **Pagination**:
   - ✅ Intelligent page breaks
   - ✅ Multi-page documents
   - ✅ Page headers and footers
   - ✅ Orphan and widow control

### Performance Requirements
- **Layout Speed**: < 100ms for typical document pages
- **Memory Usage**: Efficient memory pool utilization
- **Text Shaping**: Unicode text processing without significant lag

### Quality Requirements
- **Typography**: Professional-quality text layout
- **Precision**: Accurate positioning to 0.1 point
- **Compatibility**: Works with existing Lambda parsers
- **Extensibility**: Easy to add new layout features

## Risk Mitigation

### Technical Risks
1. **Font System Complexity**: Leverage existing Radiant font code
2. **Unicode Handling**: Use Lambda's proven ICU integration
3. **Performance Issues**: Use memory pools and optimize hot paths
4. **Mathematical Precision**: Use double precision throughout

### Schedule Risks
1. **Complex Algorithms**: Break down into smaller, testable components
2. **Integration Challenges**: Continuous integration testing
3. **Quality Issues**: Implement comprehensive test suite

## Dependencies

### External Dependencies
- **ICU Library**: For Unicode text processing and shaping
- **FreeType**: For font loading and metrics (via existing Lambda system)
- **HarfBuzz**: For advanced text shaping (optional enhancement)

### Internal Dependencies
- **Lambda Memory Pools**: For efficient allocation
- **Lambda Input Parsers**: For document content
- **Existing View Tree System**: Foundation for layout
- **SVG Renderer**: For output verification

## Future Enhancements (Post-Phase 2)

### Advanced Features
1. **Flexbox Layout**: CSS Flexbox implementation
2. **Grid Layout**: CSS Grid implementation  
3. **Table Layout**: Advanced table algorithms
4. **Float Layout**: CSS float positioning
5. **Positioned Layout**: Absolute and relative positioning

### Mathematical Typography
1. **Math Font Integration**: OpenType math fonts
2. **Mathematical Spacing**: TeX-quality math spacing
3. **Math Line Breaking**: Breaking mathematical expressions

### Advanced Typography
1. **OpenType Features**: Ligatures, kerning, variants
2. **Advanced Text Direction**: Bidirectional text (RTL/LTR)
3. **Complex Scripts**: Arabic, Thai, Indic scripts
4. **Advanced Hyphenation**: Pattern-based hyphenation

This implementation plan provides a systematic approach to building a robust layout engine that meets professional typesetting standards while integrating seamlessly with the existing Lambda architecture.
