# LaTeX to HTML Formatter Testing Framework

This directory contains a comprehensive testing framework for Lambda's LaTeX to HTML formatter, inspired by and ported from LaTeX.js testing methodology.

## Overview

The testing framework validates that Lambda's LaTeX to HTML converter produces correct HTML output for various LaTeX document structures and formatting commands.

## Architecture

### Core Components

1. **Fixture Loader** (`fixture_loader.h/cpp`)
   - Loads test fixtures from `.tex` files
   - Parses fixture format with LaTeX source and expected HTML output
   - Supports LaTeX.js-compatible fixture format

2. **HTML Comparator** (`html_comparison.h/cpp`)
   - Intelligent HTML comparison with normalization
   - Handles whitespace differences, attribute ordering
   - Provides detailed difference reporting

3. **Test Runner** (`test_latex_html_fixtures.cpp`)
   - GTest-based parameterized tests
   - Runs all fixtures automatically
   - Generates detailed failure reports

### Fixture Format

Test fixtures use the LaTeX.js format:

```
** test description
.
LaTeX source code
.
Expected HTML output
.
```

**Special Prefixes:**
- `!` - Skip test (marked as disabled)
- `s` - Screenshot test (for visual validation)
- `** ` - Test description marker

### Test Categories

#### Basic Text (`basic_text.tex`)
- Simple paragraphs and text formatting
- Special characters and UTF-8 support
- Line breaks and paragraph handling
- Verbatim text processing

#### Formatting (`formatting.tex`)
- Text formatting commands (`\textbf`, `\textit`, `\texttt`)
- Emphasis and nested formatting
- Font size commands
- Text alignment environments

#### Sectioning (`sectioning.tex`)
- Document structure (`\section`, `\subsection`, `\subsubsection`)
- Title page elements (`\title`, `\author`, `\date`, `\maketitle`)
- Multiple sections and hierarchies
- Formatted section titles

#### Environments (`environments.tex`)
- List environments (`itemize`, `enumerate`)
- Nested lists and complex structures
- Quote and verbatim environments
- Center and alignment environments
- Mixed environment combinations

## Usage

### Running Tests

```bash
# Build and run LaTeX HTML tests
make test

# Or run specific test executable
./test_latex_html_fixtures_gtest.exe
```

### Adding New Tests

1. **Create/Edit Fixture Files:**
   ```bash
   # Add tests to existing files
   vim test/latex/fixtures/basic_text.tex
   
   # Or create new fixture files
   vim test/latex/fixtures/new_category.tex
   ```

2. **Fixture Format:**
   ```
   ** descriptive test name
   .
   \LaTeX{} source code here
   .
   <div class="latex-document">
   <p>Expected HTML output here</p>
   </div>
   .
   ```

3. **Test Discovery:**
   Tests are automatically discovered from all `.tex` files in the `fixtures/` directory.

### Debugging Failed Tests

When tests fail, the framework provides detailed reports:

```
=== FIXTURE TEST FAILURE ===
File: basic_text.tex
Test: simple text formatting (ID: 1)

LaTeX Source:
-------------
\textbf{Bold text}

Expected HTML:
--------------
<div class="latex-document"><p><span class="latex-textbf">Bold text</span></p></div>

Actual HTML:
------------
<div class="latex-document"><p><strong>Bold text</strong></p></div>

Differences:
------------
Content mismatch at position 45
   Expected: <span class="latex-textbf">
   Actual:   <strong>
```

## Configuration

### HTML Comparison Settings

The HTML comparator can be configured:

```cpp
comparator.set_ignore_whitespace(true);    // Normalize whitespace
comparator.set_normalize_attributes(true); // Sort attributes
comparator.set_case_sensitive(false);      // Case-insensitive comparison
```

### Test Filtering

- Use `!` prefix to skip problematic tests during development
- Use `+` prefix to run only specific tests (LaTeX.js compatibility)
- Use `s` prefix for screenshot/visual tests (future enhancement)

## Integration with Lambda

### Parser Integration

Tests use Lambda's existing LaTeX parser:

```cpp
Item latex_ast = parse_latex_string(fixture.latex_source.c_str(), pool);
```

### Formatter Integration

Tests call Lambda's LaTeX to HTML formatter:

```cpp
format_latex_to_html(&html_buf, &css_buf, latex_ast, pool);
```

### Memory Management

All tests use Lambda's memory pool system for consistent memory management.

## Comparison with LaTeX.js

### Similarities
- Same fixture format and test structure
- Comprehensive coverage of LaTeX document features
- Automated test discovery and execution
- Detailed failure reporting

### Differences
- Uses GTest instead of Mocha/Chai
- C++ implementation instead of JavaScript
- Integration with Lambda's memory pool system
- Focus on HTML output validation (CSS generated separately)

## Future Enhancements

1. **Screenshot Testing**: Visual regression testing for complex layouts
2. **CSS Validation**: Validate generated CSS alongside HTML
3. **Performance Testing**: Benchmark LaTeX to HTML conversion speed
4. **Browser Compatibility**: Test HTML output in different browsers
5. **Math Integration**: Add mathematical expression testing when math support is integrated

## Contributing

When adding new LaTeX features to the formatter:

1. Add corresponding test fixtures
2. Ensure tests cover edge cases and error conditions
3. Update this documentation with new test categories
4. Verify all existing tests continue to pass

The testing framework ensures Lambda's LaTeX to HTML converter maintains high quality and compatibility with LaTeX document standards.
