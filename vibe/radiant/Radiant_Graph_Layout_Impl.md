# Graph Layout Implementation - Status Report

## Date: 11 January 2026

## Summary

Initiated implementation of graph layout support for Mermaid and D2 diagrams in Radiant. Created comprehensive proposal and foundational code structure, but encountered API compatibility issues that require further work.

## Completed Work

### 1. Proposal Document ✅
- Created comprehensive [vibe/Radiant_Graph_Layout.md](vibe/Radiant_Graph_Layout.md)
- Documented Dagre algorithm phases (rank assignment, crossing reduction, coordinate assignment, edge routing)
- Specified SVG generation approach
- Outlined 5-week implementation timeline

### 2. Core Data Structures ✅
- `radiant/graph_layout_types.hpp` - Complete type definitions for layout system
- Defined: NodePosition, EdgePath, GraphLayout, LayoutNode, LayoutEdge, LayoutLayer, LayoutGraph
- Clean separation between public API (GraphLayout) and internal structures (LayoutGraph)

### 3. Main Layout Coordinator ✅ (Needs Fixes)
- `radiant/layout_graph.hpp` + `.cpp` - Main layout API
- Functions: `layout_graph()`, `layout_graph_with_algorithm()`, `layout_graph_with_options()`
- Graph parsing from Lambda Element tree
- Algorithm dispatch to Dagre

### 4. Dagre Algorithm Implementation ✅ (Needs Fixes)
- `radiant/graph_dagre.hpp` + `.cpp` - Hierarchical graph layout
- **Phase 1**: Rank assignment via longest-path DFS
- **Phase 2**: Layer creation from ranks
- **Phase 3**: Crossing reduction using barycenter heuristic
- **Phase 4**: Coordinate assignment (grid-based)
- **Phase 5**: Edge routing (straight lines, splines planned)

### 5. SVG Generation ✅ (Needs Fixes)
- `radiant/graph_to_svg.hpp` + `.cpp` - SVG output from layout
- Node shape rendering: rect, circle, ellipse, diamond, hexagon, triangle
- Edge path rendering with arrow markers
- Label positioning
- Styling from graph attributes

### 6. CLI Integration ✅ (Needs Fixes)
- Modified `lambda/main.cpp` render command
- Added support for `.mmd`, `.d2`, `.dot`, `.gv` input formats
- Integrated layout → SVG → output pipeline
- Output formats: SVG (direct), PDF, PNG, JPEG (via temp SVG)

### 7. Build Configuration ✅
- Updated `build_lambda_config.json` with new radiant files
- Added: `layout_graph.cpp`, `graph_dagre.cpp`, `graph_to_svg.cpp`

## Issues Encountered

### API Compatibility Problems ❌

**ArrayList API Mismatch:**
- Used `arraylist_at()` which doesn't exist
- Actual API: `arraylist_get()` from lib/arraylist.h takes different parameters
- Need to study lib/arraylist.h API carefully

**HashMap Removal:**
- Initial design used hashmaps for node lookups
- Lambda's hashmap API (lib/hashmap.h) is complex, struct-based
- Refactored to use ArrayLists with linear search (simpler)

**Lambda Data API:**
- Many function naming issues: `is_string()`, `is_element()`, `is_list()`
- Actual API uses Item type system differently
- Need to properly use `get_type_id()` and typed accessors

**MarkBuilder/MarkReader API:**
- Incorrect usage of createElement, setAttribute
- MarkBuilder returns `Item`, not `Element*`
- Need to study mark_builder.hpp API carefully

**Element Structure:**
- Accessed `element->attrs.map` directly, but structure may be different
- Accessed `element->children.list`, but may need different approach
- Need to properly use MarkReader for Element traversal

**Missing Includes:**
- Several C functions not properly declared
- Need proper includes for: input_create, input_destroy, format_svg, arraylist APIs

## Next Steps to Complete Implementation

###  1. Fix ArrayList API Usage (High Priority)
**File:** `lib/arraylist.h`
```c
// Study actual API - likely need to use:
void* arraylist_get(ArrayList* list, size_t index);  // get element at index
void arraylist_add(ArrayList* list, void* item);     // add element
ArrayList* arraylist_new(size_t capacity);           // create new list
```

**Action:** Replace all `arraylist_at()` calls with correct API

### 2. Fix Lambda Data Access Patterns (High Priority)
**Reference files:**
- `lambda/lambda-data.hpp` - Item type system
- `lambda/mark_reader.hpp` - Element traversal
- `lambda/mark_builder.hpp` - Element construction

**Patterns to study:**
```cpp
// Check type
TypeId type = get_type_id(item);
if (type == LMD_TYPE_STRING) {
    String* str = item.string_ptr;
}

// Access Element
if (type == LMD_TYPE_ELEMENT) {
    Element* elem = item.element;
    // use MarkReader to traverse
}
```

**Action:** Rewrite layout_graph.cpp and graph_to_svg.cpp with correct API

### 3. Fix MarkBuilder Usage (Medium Priority)
**Current issues:**
- `createElement()` returns `Item`, not `Element*`
- `setAttribute()` may not exist or takes different parameters
- Need to properly create SVG element tree

**Action:** Study existing radiant code for SVG generation patterns (e.g., `radiant/render_svg.cpp`)

### 4. Add Missing Includes (Medium Priority)
**Files to update:**
- `lambda/main.cpp` - add proper input/format includes
- `radiant/layout_graph.cpp` - add arraylist.h properly
- `radiant/graph_dagre.cpp` - add arraylist.h properly

### 5. Testing Strategy (After Fixes)
**Create test files:**
```
test/graph/simple_flowchart.mmd
test/graph/branching_diagram.d2
test/graph/architecture.dot
```

**Test commands:**
```bash
./lambda.exe render test/graph/simple_flowchart.mmd -o output.svg
./lambda.exe render test/graph/branching_diagram.d2 -o output.png
```

## Estimated Time to Complete

- **API fixes**: 2-3 hours (careful study + systematic fixes)
- **Testing**: 1 hour (create test files, verify output)
- **Polish**: 1 hour (error handling, edge cases)

**Total**: 4-5 hours of focused work

## Files Created/Modified

### New Files (7)
1. `vibe/Radiant_Graph_Layout.md` - Proposal document
2. `radiant/graph_layout_types.hpp` - Type definitions
3. `radiant/layout_graph.hpp` - Main API header
4. `radiant/layout_graph.cpp` - Main implementation
5. `radiant/graph_dagre.hpp` - Dagre algorithm header
6. `radiant/graph_dagre.cpp` - Dagre implementation
7. `radiant/graph_to_svg.hpp` + `.cpp` - SVG generation

### Modified Files (2)
1. `lambda/main.cpp` - CLI integration
2. `build_lambda_config.json` - Build system

## Code Quality Notes

**Strengths:**
- Clean separation of concerns (layout vs rendering)
- Well-documented algorithm phases
- Modular design for easy testing
- Comprehensive type system

**Areas Needing Improvement:**
- API compatibility (main blocker)
- Error handling (add validation)
- Memory management (verify all allocations freed)
- Performance (optimize linear searches if needed)

## Recommendations

1. **Short-term**: Focus on API compatibility fixes to get a working prototype
2. **Medium-term**: Add comprehensive tests and error handling
3. **Long-term**: Optimize performance, add advanced features (spline edges, interactive layouts)

## References

- **Proposal**: [vibe/Radiant_Graph_Layout.md](vibe/Radiant_Graph_Layout.md)
- **Input Parsers**: [lambda/input/input-graph-mermaid.cpp](lambda/input/input-graph-mermaid.cpp), [input-graph-d2.cpp](lambda/input/input-graph-d2.cpp)
- **Lambda APIs**: [lambda/lambda-data.hpp](lambda/lambda-data.hpp), [lambda/mark_builder.hpp](lambda/mark_builder.hpp)
- **Dagre Algorithm**: See proposal Appendix B for papers and references

---

**Status**: 🟡 Foundation Complete, API Fixes Required
**Next Owner**: Developer with Lambda API knowledge
**Priority**: Medium (valuable feature, non-blocking)
