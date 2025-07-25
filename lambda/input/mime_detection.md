# MIME Type Detection for Lambda

This implementation provides Apache Tika-like MIME type detection capabilities to the Lambda project, enabling automatic file type detection and parser selection.

## Overview

The MIME detection system allows Lambda to automatically identify file types based on content analysis and filename patterns, eliminating the need to manually specify parsers for different file formats.

## File Structure

- **`mime-detect.h`**: Header file with MIME detection API and external data declarations
- **`mime-detect.c`**: Core detection implementation  
- **`mime-types.c`**: MIME type database with patterns and file extension mappings
- **Integration with `input.c`**: Modified to support auto-detection

## Usage

### Basic API Usage

```c
// Initialize detector
MimeDetector* detector = mime_detector_init();

// Detect MIME type from filename and content
const char* mime = detect_mime_type(detector, "document.pdf", content, content_len);

// Detect from filename only
const char* mime = detect_mime_from_filename(detector, "script.js");

// Detect from content only
const char* mime = detect_mime_from_content(detector, content, content_len);

// Cleanup
mime_detector_destroy(detector);
```

### Lambda Script Usage

```javascript
// Auto-detect file type based on content and extension
let data = input("document.pdf", "auto");         // -> PDF parser
let json_data = input("data.json", "auto");       // -> JSON parser
let html_page = input("index.html", "auto");      // -> HTML parser
let csv_file = input("spreadsheet.csv", "auto");  // -> CSV parser

// Works with files without extensions (content-based detection)
let unknown_file = input("datafile", "auto");     // -> Detects based on content
```

## Detection Logic

The MIME detector uses a multi-layered approach:

1. **Content-based detection** (primary):
   - Checks magic byte patterns in file header
   - Validates specific format signatures (PDF magic bytes, HTML DOCTYPE, XML declarations, etc.)
   - Assigns priority scores to matches
   - Supports shebang detection for scripts (`#!/bin/bash`, `#!/usr/bin/env python`)

2. **Filename-based detection** (secondary):
   - Matches file extensions against known patterns
   - Supports case-insensitive matching
   - Uses glob patterns for flexibility

3. **Content validation and subtypes**:
   - Validates detected types (e.g., confirms ZIP is actually DOCX)
   - Distinguishes text from binary content
   - Provides intelligent fallbacks

4. **Parser mapping**:
   - Maps MIME types to Lambda's internal parsers
   - Handles format variations (e.g., `application/xml` and `text/xml`)
   - Provides sensible defaults for unknown types

## Supported MIME Types → Parser Mappings

| MIME Type | Lambda Parser | Description |
|-----------|---------------|-------------|
| `application/json` | `json` | JSON data |
| `text/csv` | `csv` | Comma-separated values |
| `application/xml`, `text/xml` | `xml` | XML documents |
| `text/html` | `html` | HTML pages |
| `text/markdown` | `markdown` | Markdown text |
| `text/x-rst` | `rst` | reStructuredText |
| `application/rtf` | `rtf` | Rich Text Format |
| `application/pdf` | `pdf` | PDF documents |
| `application/x-latex`, `application/x-tex` | `latex` | LaTeX documents |
| `application/toml` | `toml` | TOML configuration |
| `application/x-yaml` | `yaml` | YAML data |
| `text/x-python` | `text` | Python scripts |
| `application/x-shellscript` | `text` | Shell scripts |
| `application/javascript` | `text` | JavaScript code |
| `text/plain` | `text` | Plain text (fallback) |

## Detection Patterns

### Magic Byte Patterns

The system recognizes files by their magic bytes (binary signatures):

- **PDF**: `%PDF-` at start of file
- **PNG**: `\x89PNG\r\n\x1a\n` signature
- **JPEG**: `\xff\xd8\xff` header
- **ZIP**: `PK\x03\x04` or `PK\x05\x06`
- **HTML**: `<!DOCTYPE html>`, `<html>`, `<head>`, `<body>` tags
- **XML**: `<?xml` declaration
- **Scripts**: Shebang patterns like `#!/bin/bash`, `#!/usr/bin/env python`

### File Extension Patterns

Common file extensions are mapped to MIME types:

- **Documents**: `.pdf`, `.doc`, `.docx`, `.rtf`
- **Text**: `.txt`, `.csv`, `.html`, `.xml`, `.json`, `.md`
- **Programming**: `.c`, `.h`, `.py`, `.js`, `.css`, `.sh`
- **Images**: `.jpg`, `.png`, `.gif`, `.svg`, `.bmp`
- **Archives**: `.zip`, `.tar`, `.gz`, `.7z`

## Testing

### Criterion Unit Tests

The MIME detection system includes comprehensive unit tests using the Criterion testing framework:

#### Test Setup

```bash
# Install Criterion (macOS)
brew install criterion

# Compile and run tests
gcc -std=c99 -g -I. -I/opt/homebrew/opt/criterion/include \\
    -L/opt/homebrew/opt/criterion/lib -lcriterion \\
    test/test_mime_detect.c lambda/input/mime-detect.c lambda/input/mime-types.c \\
    -o test_mime_detect

# Run tests
./test_mime_detect
```

#### Test Coverage

The test suite includes:

1. **Basic Detection Tests** (`mime_detect_tests::basic_detection`):
   - Tests fundamental MIME type detection functionality
   - Validates JSON, HTML, and other common formats

2. **Filename Detection** (`mime_detect_tests::filename_detection`):
   - Tests extension-based detection
   - Verifies case-insensitive matching

3. **Content Detection** (`mime_detect_tests::content_detection`):
   - Tests magic byte pattern matching
   - Validates content-based detection without filenames

4. **Magic Bytes** (`mime_detect_tests::magic_bytes`):
   - Tests specific binary signatures (PDF, images, etc.)
   - Validates priority-based matching

5. **Extensionless Files** (`mime_detect_tests::extensionless_files`):
   - Tests files without extensions
   - Validates content-based detection for various formats:
     - XML documents
     - HTML pages
     - CSV data (detected as text/plain - expected)
     - Markdown documents
     - YAML configs (detected as text/plain - expected)
     - JavaScript code (detected as text/plain without shebang - expected)
     - Python scripts with shebangs
     - Shell scripts with shebangs
     - PDF documents

6. **Test Input Files** (`mime_detect_tests::test_input_files`):
   - Tests against actual files in `test/input/` directory
   - Validates both extension-based and content-based detection

7. **Edge Cases** (`mime_detect_tests::edge_cases`):
   - Tests empty content handling
   - Tests NULL filename scenarios
   - Tests binary content detection

8. **Specific Mappings** (`mime_detect_tests::specific_mappings`):
   - Tests particular MIME type mappings
   - Validates JavaScript, CSS, XML, TOML, and Markdown detection

#### Test Results

All 9 test suites pass successfully:

```
[====] Synthesis: Tested: 9 | Passing: 9 | Failing: 0 | Crashing: 0
```

Example test output:
```
✓ xml_content -> application/xml (content-based)
✓ html_content -> text/html (content-based)
✓ csv_data -> text/plain (content-based)
✓ markdown_doc -> text/markdown (content-based)
✓ python_script -> text/x-python (content-based)
✓ shell_script -> application/x-shellscript (content-based)
✓ pdf_document -> application/pdf (content-based)
```

#### Test Files

The test suite uses files in `test/input/` including:

**Files with Extensions:**
- `test.json`, `test.html`, `test.xml`, `test.csv`
- `test.pdf`, `test.md`, `test.yaml`, `test.toml`
- `test.ini`, `test.rst`, `test.rtf`, `test.tex`

**Files without Extensions (Content-based Detection):**
- `xml_content` - XML document
- `html_content` - HTML page
- `csv_data` - CSV data
- `markdown_doc` - Markdown text
- `config_yaml` - YAML configuration
- `plain_text` - Plain text
- `script_content` - JavaScript code
- `python_script` - Python with shebang
- `shell_script` - Shell script with shebang
- `pdf_document` - PDF file

## Build Integration

The MIME detector is automatically included in the build:

- Source files are in `lambda/input/` directory (scanned by build system)
- No additional build configuration required
- Compatible with existing Lambda build process
- Cross-platform support (macOS, Linux, Windows)

## Performance Considerations

- **Minimal overhead**: Only processes file headers (first few KB)
- **Efficient patterns**: Sorted by priority for fast matching
- **Memory efficient**: Static data structures, minimal allocations
- **Early termination**: Stops at first high-priority match

## Error Handling

- **Graceful fallbacks**: Always returns a valid MIME type
- **Null safety**: Handles missing files and invalid inputs
- **Default behavior**: Falls back to `text/plain` or `application/octet-stream`
- **Logging**: Provides debug information about detection process

## Limitations and Considerations

### Content-based Detection Limitations

Some file types are inherently difficult to detect without extensions:

- **CSV files**: Often detected as `text/plain` since they're just comma-separated text
- **YAML files**: May be detected as `text/plain` unless they have distinctive patterns
- **JavaScript without shebangs**: Detected as `text/plain` when lacking clear identifiers
- **Plain configuration files**: Often ambiguous without context

### Recommended Usage

- **Use extensions when possible**: Filename-based detection is more reliable for ambiguous formats
- **Content-based detection works best for**: PDF, HTML, XML, binary formats, scripts with shebangs
- **Fallback handling**: Always handle the `text/plain` case gracefully in your application

This implementation successfully provides Apache Tika-like MIME detection capabilities while maintaining compatibility with the existing Lambda input system.
