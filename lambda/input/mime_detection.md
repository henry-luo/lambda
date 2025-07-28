# MIME Type Detection for Lambda

This implementation provides Apache Tika-like MIME type detection capabilities to the Lambda project, enabling automatic file type detection and parser selection.

## Overview

The MIME detection system allows Lambda to automatically identify file types based on content analysis and filename patterns, eliminating the need to manually specify parsers for different file formats. The system is integrated into the Lambda input system and supports intelligent auto-detection with fallback mechanisms.

## File Structure

- **`mime-detect.h`**: Header file with MIME detection API and data structure definitions
- **`mime-detect.c`**: Core detection implementation with priority-based matching
- **`mime-types.c`**: Comprehensive MIME type database with magic patterns and file extension mappings
- **Integration with `input.c`**: Full integration with Lambda's input system for automatic parser selection

## Usage

### Basic API Usage

```c
// Initialize detector
MimeDetector* detector = mime_detector_init();

// Detect MIME type from filename and content (recommended)
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

### CLI Usage with Validation

The MIME detection is also integrated with the Lambda validator for automatic format detection:

```bash
# Auto-detect format and use appropriate schema
./lambda.exe validate document.html    # -> Detects HTML, uses html5_schema.ls
./lambda.exe validate data.json -s custom_schema.ls  # -> Detects JSON format
```

## Detection Logic

The MIME detector uses a sophisticated multi-layered approach with priority-based matching:

1. **Hybrid Detection Strategy** (Primary):
   - **Priority-based content detection**: Checks magic byte patterns with assigned priority scores
   - **Smart fallback logic**: Combines filename and content detection intelligently
   - **High-priority binary formats**: PDF, images, archives get priority over filename
   - **Text format preference**: For ambiguous cases, filename detection takes precedence

2. **Content-based detection** features:
   - **Magic byte pattern matching**: Validates specific format signatures (PDF `%PDF-`, PNG `\x89PNG`, etc.)
   - **Priority scoring system**: Higher priority patterns (50-60) override lower priority ones (20-30)
   - **Shebang detection**: Recognizes script interpreters (`#!/bin/bash`, `#!/usr/bin/env python`)
   - **Text vs binary classification**: Analyzes character distribution to classify content
   - **Subtype validation**: Validates detected types (e.g., confirms ZIP is actually DOCX/EPUB)

3. **Filename-based detection** features:
   - **Case-insensitive matching**: Handles mixed-case file extensions
   - **Glob pattern support**: Uses `*` and `?` wildcards for flexible matching
   - **Comprehensive extension database**: 80+ file extensions mapped to MIME types

4. **Intelligent fallback chain**:
   - **Content validation**: Validates detected formats (JSON must start with `{` or `[`)
   - **Text detection**: Uses statistical analysis to identify text content (70% threshold)
   - **Binary classification**: Defaults to `application/octet-stream` for unknown binary data
   - **Parser mapping**: Automatically maps MIME types to Lambda parsers

## Supported MIME Types → Parser Mappings

| MIME Type | Lambda Parser | Description | Detection Method |
|-----------|---------------|-------------|------------------|
| `application/json` | `json` | JSON data | Content (`{`/`[`) + extension |
| `text/csv` | `csv` | Comma-separated values | Extension + weak content signal |
| `application/xml`, `text/xml` | `xml` | XML documents | Content (`<?xml`) + extension |
| `text/html` | `html` | HTML pages | Content (DOCTYPE, tags) |
| `text/markdown` | `markdown` | Markdown text | Extension + content (`#` headers) |
| `text/x-rst` | `rst` | reStructuredText | Extension-based |
| `application/rtf` | `rtf` | Rich Text Format | Content (`{\\rtf`) |
| `application/pdf` | `pdf` | PDF documents | Content (`%PDF-`) |
| `application/x-latex`, `application/x-tex` | `latex` | LaTeX documents | Extension-based |
| `application/toml` | `toml` | TOML configuration | Extension-based |
| `application/x-yaml` | `yaml` | YAML data | Extension-based |
| `text/vcard` | `vcf` | vCard contacts | Content (`BEGIN:VCARD`) |
| `text/calendar` | `ics` | iCalendar events | Extension-based |
| `message/rfc822`, `application/eml` | `eml` | Email messages | Extension-based |
| `text/x-python` | `text` | Python scripts | Shebang detection |
| `application/x-shellscript` | `text` | Shell scripts | Shebang detection |
| `application/javascript` | `text` | JavaScript code | Extension-based |
| `text/plain` | `text` | Plain text (fallback) | Text analysis |
| `application/octet-stream` | `text` | Binary data (fallback) | Binary classification |

### Special Handling

- **Office Documents**: ZIP containers validated for DOCX/XLSX/PPTX MIME types in content
- **WebP/WAV**: RIFF containers with subtype validation
- **Archives**: ZIP detection with EPUB/Office document subtype checking
- **Programming Languages**: Comprehensive support for C/C++, Java, Python, JavaScript, etc.
- **Image Formats**: Full support for JPEG, PNG, GIF, TIFF, BMP, SVG with magic byte validation

## Detection Patterns

### Magic Byte Patterns (Priority-Based)

The system recognizes files by their binary signatures with priority scores:

**High Priority (50-60):**
- **PDF**: `%PDF-` at start of file (priority 50)
- **HTML**: `<!DOCTYPE html>`, `<html>`, `<head>`, `<body>` tags (priority 50-60)
- **PNG**: `\x89PNG\r\n\x1a\n` signature (priority 50)
- **JPEG**: `\xff\xd8\xff` header (priority 50)
- **ZIP/Office**: `PK\x03\x04` or `PK\x05\x06` (priority 50)
- **XML**: `<?xml` declaration (priority 50)
- **Scripts**: Shebang patterns (priority 55)

**Medium Priority (30-40):**
- **JSON**: `{` or `[` at start (priority 30)
- **Markdown**: `#` headers (priority 30)
- **WebP/WAV**: RIFF containers requiring validation (priority 40)

**Low Priority (10-20):**
- **CSV**: `,` comma detection (priority 20, very weak signal)
- **UTF-8 BOM**: `\xef\xbb\xbf` (priority 10)

### File Extension Patterns

Comprehensive glob pattern database with 80+ extensions:

**Document Formats:**
- Office: `.doc`, `.docx`, `.xls`, `.xlsx`, `.ppt`, `.pptx`
- Open Document: `.odt`, `.ods`, `.odp`
- Text: `.pdf`, `.rtf`, `.txt`, `.md`, `.rst`, `.tex`

**Data Formats:**
- Structured: `.json`, `.xml`, `.yaml`, `.toml`, `.csv`, `.ini`
- Contacts/Calendar: `.vcf`, `.vcard`, `.ics`, `.ical`

**Programming Languages:**
- C/C++: `.c`, `.h`, `.cpp`, `.hpp`, `.cxx`, `.hxx`
- Web: `.js`, `.ts`, `.html`, `.htm`, `.css`, `.php`
- Scripts: `.py`, `.rb`, `.pl`, `.sh`, `.bash`

**Media Files:**
- Images: `.jpg`, `.png`, `.gif`, `.bmp`, `.svg`, `.webp`
- Audio: `.mp3`, `.wav`, `.ogg`, `.flac`, `.aac`
- Video: `.mp4`, `.avi`, `.mov`, `.webm`, `.mkv`

**Archives & Executables:**
- Archives: `.zip`, `.rar`, `.7z`, `.tar`, `.gz`
- Executables: `.exe`, `.msi`, `.deb`, `.rpm`, `.dmg`

## Testing

### Comprehensive Criterion Unit Tests

The MIME detection system includes extensive unit tests using the Criterion testing framework with 9 test suites covering all functionality:

#### Test Setup and Compilation

```bash
# Install Criterion (macOS)
brew install criterion

# Compile and run tests
gcc -std=c99 -g -I. -I/opt/homebrew/opt/criterion/include \
    -L/opt/homebrew/opt/criterion/lib -lcriterion \
    test/test_mime_detect.c lambda/input/mime-detect.c lambda/input/mime-types.c \
    -o test_mime_detect

# Run tests with detailed output
./test_mime_detect --verbose
```

#### Comprehensive Test Coverage (9 Test Suites)

**1. Basic Detection Tests** (`mime_detect_tests::basic_detection`):
   - Tests fundamental MIME type detection functionality
   - Validates JSON, HTML, and other common formats
   - Tests combined filename and content detection

**2. Filename Detection** (`mime_detect_tests::filename_detection`):
   - Tests extension-based detection with case-insensitive matching
   - Validates glob pattern matching (`*.jpg`, `*.html`, etc.)
   - Tests filename-only detection scenarios

**3. Content Detection** (`mime_detect_tests::content_detection`):
   - Tests magic byte pattern matching without filenames
   - Validates binary signature recognition (PDF, PNG, JPEG)
   - Tests content-based detection for text formats

**4. Magic Bytes Priority System** (`mime_detect_tests::magic_bytes`):
   - Tests specific binary signatures with priority ordering
   - Validates PDF (`%PDF-`), PNG, JPEG magic bytes
   - Tests priority-based conflict resolution

**5. Extensionless Files** (`mime_detect_tests::extensionless_files`):
   - Tests files without extensions using pure content analysis
   - Validates detection of:
     - XML documents (`<?xml` detection)
     - HTML pages (DOCTYPE and tag detection)
     - CSV data (comma-based, detected as `text/plain`)
     - Markdown documents (`#` header detection)
     - YAML configs (detected as `text/plain` without distinctive patterns)
     - JavaScript code (detected as `text/plain` without shebang)
     - Python scripts with shebangs (`#!/usr/bin/env python`)
     - Shell scripts with shebangs (`#!/bin/bash`)
     - PDF documents (magic byte detection)

**6. Real File Testing** (`mime_detect_tests::test_input_files`):
   - Tests against actual files in `test/input/` directory
   - Validates both extension-based and content-based detection
   - Tests real-world file scenarios

**7. Edge Cases** (`mime_detect_tests::edge_cases`):
   - Tests empty content handling and graceful fallbacks
   - Tests NULL filename scenarios and error handling
   - Tests binary content classification

**8. Specific MIME Mappings** (`mime_detect_tests::specific_mappings`):
   - Tests particular MIME type to parser mappings
   - Validates JavaScript, CSS, XML, TOML detection
   - Tests parser integration with Lambda input system

**9. Advanced Content Validation** (`mime_detect_tests::content_validation`):
   - Tests subtype detection (ZIP → DOCX validation)
   - Tests content validation (JSON structure validation)
   - Tests text vs binary classification algorithm

#### Test Results and Success Metrics

All test suites pass successfully with comprehensive coverage:

```
[====] Synthesis: Tested: 9 | Passing: 9 | Failing: 0 | Crashing: 0
```

**Example successful detections:**
```
✓ test.pdf -> application/pdf (magic byte detection)
✓ document.json -> application/json (extension + content validation)
✓ index.html -> text/html (DOCTYPE detection)
✓ data.xml -> application/xml (<?xml declaration)
✓ script.py -> text/x-python (shebang detection)
✓ archive.zip -> application/zip (PK magic bytes)
✓ image.png -> image/png (PNG signature)
```

#### Test Files and Input Data

The test suite uses a comprehensive collection of test files:

**Files with Extensions (Extension-Based Detection):**
- **Documents**: `test.pdf`, `document.docx`, `presentation.pptx`
- **Data**: `test.json`, `data.xml`, `config.yaml`, `settings.toml`
- **Web**: `test.html`, `style.css`, `script.js`
- **Text**: `test.csv`, `readme.md`, `document.rst`, `article.tex`
- **Contact/Calendar**: `contact.vcf`, `event.ics`
- **Email**: `message.eml`

**Files without Extensions (Content-Based Detection):**
- `xml_content` - XML document with `<?xml` declaration
- `html_content` - HTML page with DOCTYPE
- `csv_data` - Comma-separated values
- `markdown_doc` - Markdown with `#` headers
- `config_yaml` - YAML configuration data
- `plain_text` - Simple text content
- `script_content` - JavaScript code
- `python_script` - Python with `#!/usr/bin/env python` shebang
- `shell_script` - Bash with `#!/bin/bash` shebang
- `pdf_document` - PDF with `%PDF-` magic bytes

## Performance Characteristics

- **Minimal overhead**: Only processes file headers (first 1-2KB for most formats)
- **Efficient pattern matching**: Priority-sorted patterns for fast termination
- **Memory efficient**: Static pattern database, minimal heap allocations
- **Early termination**: Stops at first high-priority match
- **Cache-friendly**: Linear search through sorted arrays
- **Text analysis optimization**: 70% character threshold for text classification

## Error Handling and Robustness

- **Graceful fallbacks**: Always returns a valid MIME type or parser
- **Null safety**: Handles missing files, empty content, and invalid inputs
- **Default behavior**: Falls back to `text/plain` or `application/octet-stream`
- **Memory safety**: Proper bounds checking and buffer validation
- **Debug information**: Provides detailed logging for detection process
- **Validation layers**: Multiple validation steps for complex formats

## Limitations and Considerations

### Content-based Detection Limitations

Some file types are inherently difficult to detect without extensions due to their text-based nature:

- **CSV files**: Often detected as `text/plain` since they're just comma-separated text
- **YAML files**: May be detected as `text/plain` unless they have distinctive YAML-specific patterns
- **JavaScript without shebangs**: Detected as `text/plain` when lacking clear identifiers or shebangs
- **Plain configuration files**: Often ambiguous without context (INI, properties files)
- **Generic data formats**: Simple structured text may not have distinctive patterns

### Format Ambiguity

- **RIFF containers**: WebP and WAV both use RIFF format, requiring subtype validation
- **ZIP-based formats**: Office documents, EPUB, and JAR files all use ZIP container format
- **XML variants**: RSS, SOAP, SVG all appear as generic XML without namespace analysis
- **Text-based data**: JSON, YAML, TOML may overlap with plain text in edge cases

### Recommended Usage Patterns

- **Extension-first strategy**: Use extensions when available for most reliable detection
- **Content validation**: Combine with content analysis for verification
- **Format-specific handling**: Handle `text/plain` fallback cases appropriately in applications
- **Parser integration**: Let the Lambda input system handle parser mapping automatically

### Best Practices

1. **For file processing**: Always handle both detected MIME type and fallback cases
2. **For validation**: Use content detection to verify file integrity matches extension
3. **For user interfaces**: Display both detected format and confidence level when possible
4. **For debugging**: Enable verbose logging to understand detection decisions

## Future Enhancement Opportunities

### Advanced Detection Features
- **Enhanced heuristics**: Improve detection of ambiguous text-based formats
- **Machine learning**: Train classifiers for better content-based detection
- **Metadata extraction**: Extract and use embedded metadata for format validation
- **Streaming detection**: Support large files with streaming analysis

### Format Support Expansion
- **Modern formats**: Add support for WebAssembly, Protocol Buffers, and other emerging formats
- **Specialized formats**: CAD files, scientific data formats, blockchain formats
- **Legacy support**: Better detection of older document and image formats

### Performance Optimizations
- **Parallel processing**: Multi-threaded detection for large file sets
- **Caching**: Cache detection results for repeated file access
- **Profile-based**: Optimize patterns based on usage statistics

- **Use extensions when possible**: Filename-based detection is more reliable for ambiguous formats
- **Content-based detection works best for**: PDF, HTML, XML, binary formats, scripts with shebangs
- **Fallback handling**: Always handle the `text/plain` case gracefully in your application

This implementation successfully provides enterprise-grade MIME detection capabilities comparable to Apache Tika, while maintaining seamless integration with the Lambda input system and providing robust fallback mechanisms for edge cases.
