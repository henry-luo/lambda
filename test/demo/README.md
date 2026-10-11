# Lambda demos

Run `./lambda.exe demo <name>` from the repository root, or `./lambda demo <name>`
from an unpacked release. Each named demo uses the same viewer as `lambda view`.
Run `lambda demo --list` for the catalog. With no name, `lambda demo` opens the
bundled document viewer with its startup splash (D7.2.4).

| Name | Entry point | Guide |
| --- | --- | --- |
| `tetris` | `tetris/tetris.ls` | [Tetris](tetris/TETRIS.md) |
| `superlambda` | `superlambda/superlambda.ls` | [Superlambda](superlambda/README.md) |
| `doom` | `doom/doom.ls` | [DOOM](doom/README.md) |
| `photo` | `photo/photo.ls` | [Photo Studio](photo/README.md) |
| `scene3d`, `ringworld` | `scene3d/ringworld.ls` | [Native 3D](scene3d/README.md) |
| `observatory` | `scene3d/observatory.html` | [Native 3D](scene3d/README.md) |
| `three-gallery` | `scene3d/three-gallery.html` | [Native 3D](scene3d/README.md) |
| `asset-gallery` | `scene3d/asset-gallery.ls` | [Native 3D](scene3d/README.md) |
| `shared-animation` | `scene3d/shared-animation.html` | [Native 3D](scene3d/README.md) |
| `slides` | `slides/northstar.slides` | [Slides](slides/README.md) |
| `wordcloud` | `wordcloud.ls` | [Charts](CHARTS.md) |

Paths in the table are relative to `test/demo/`. Viewer options follow the name:

```sh
./lambda.exe demo tetris
./lambda.exe demo scene3d
./lambda.exe demo tetris --headless --event-file test/ui/tetris_autoplay.json \
  --event-result temp/tetris-demo.json
```

The games and Ringworld include an **Auto Play** button. Direct `lambda view`
commands and individual test/replay commands remain available in each guide.

## Readability extraction

This demo replicates the functionality of [Mozilla's Readability](https://github.com/mozilla/readability) library using Lambda Script's functional programming capabilities and multi-format I/O system.

## Overview

The readability extraction system identifies and extracts the main content from web pages while filtering out navigation, advertisements, comments, and other non-essential elements.

## Files

- **`readability.ls`** - Main readability module implementing content extraction
- **`test_readable.ls`** - Test script demonstrating the readability functionality  
- **`sample_article.html`** - Sample HTML article with typical web page structure
- **`README.md`** - This documentation file

## Features

### Content Extraction Algorithm

The algorithm uses a scoring system inspired by Mozilla Readability:

1. **Element Scoring**: Elements are scored based on:
   - HTML tag types (`article`, `main`, `section` = positive; `nav`, `footer`, `aside` = negative)
   - Class names and IDs (content indicators vs. navigation/ad indicators)  
   - Content length and text-to-link ratio

2. **Content Detection**: Identifies substantial content blocks by:
   - Minimum word count thresholds
   - Text-to-link ratio analysis
   - Semantic HTML structure analysis

3. **Content Cleaning**: Removes unwanted elements:
   - Navigation menus and footers
   - Advertisement blocks  
   - Comment sections
   - Social media widgets
   - Scripts and styles

### Exported Functions

```lambda
// Main extraction function
pub fn transform(parsed_html) -> readable_result

// Markdown formatting helper  
pub fn format_readable_markdown(readable_result) -> string
```

### Return Structure

The `transform()` function returns a comprehensive result object:

```lambda
{
    title: "Extracted page title",
    author: "Article author", 
    date: "Publication date",
    description: "Meta description",
    content: cleaned_html_structure,
    text_content: "Plain text content",
    word_count: 1234,
    score: 85,
    success: true,
    message: "Content extracted successfully",
    debug: {
        candidates_found: 5,
        selected_tag: "article", 
        selected_score: 85
    }
}
```

## Usage

### Running the Demo

```bash
# From the project root directory
lambda test/demo/test_readable.ls
```

### Using in Your Scripts

```lambda
// Import the readability module
import readability: ./test/demo/readability;

// Load and parse HTML
let html = input("webpage.html", 'html')

// Extract readable content
let result = readability.transform(html)

// Generate markdown output
let markdown = readability.format_readable_markdown(result)
```

## Algorithm Details

### Scoring Weights

```lambda
let SCORE_WEIGHTS = {
    // Positive indicators (+)
    article: 25, main: 25, content: 15, post: 15,
    
    // Negative indicators (-)  
    nav: -25, footer: -25, sidebar: -25, ad: -15
}
```

### Content Indicators

**Positive Patterns**: `article`, `content`, `entry`, `main`, `post`, `text`, `story`

**Negative Patterns**: `nav`, `footer`, `sidebar`, `ad`, `comment`, `social`, `widget`

### Quality Thresholds

- **Minimum content length**: 25 characters
- **Minimum paragraph length**: 25 characters  
- **Maximum link-to-text ratio**: 30%
- **Minimum word count**: 10 words

## Advantages of Lambda Script Implementation

1. **Functional Programming**: Pure functional approach makes the algorithm predictable and testable

2. **Multi-Format I/O**: Native support for HTML parsing and multiple output formats (JSON, XML, Markdown, etc.)

3. **Type Safety**: Strong typing prevents common content extraction errors

4. **Memory Efficiency**: Advanced memory pooling handles large documents efficiently  

5. **Cross-Platform**: Consistent behavior across macOS, Linux, Windows

6. **Performance**: JIT compilation provides near-native execution speed

## Comparison with Mozilla Readability

| Feature | Mozilla Readability | Lambda Script Implementation |
|---------|-------------------|----------------------------|
| Language | JavaScript | Lambda Script (functional) |
| Scoring System | ✅ Similar algorithm | ✅ Adapted for Lambda |
| Content Cleaning | ✅ DOM manipulation | ✅ Functional transformations |
| Metadata Extraction | ✅ Basic | ✅ Enhanced with multi-format |
| Output Formats | HTML/Text | ✅ HTML/Text/Markdown/JSON/XML |
| Performance | Fast | ✅ JIT-compiled, very fast |

## Real-World Applications

This readability implementation can be used for:

1. **Content Aggregation**: Extract articles from news websites
2. **Research Tools**: Clean academic papers and documentation  
3. **Archive Systems**: Create clean versions of web content
4. **Reading Apps**: Generate reader-friendly content  
5. **Content Analysis**: Analyze writing quality and structure
6. **SEO Tools**: Extract main content for search optimization

## Testing

The demo includes a comprehensive test article (`sample_article.html`) that contains:

- Real article content with multiple sections
- Navigation menus and sidebars  
- Advertisement blocks
- Comment sections
- Social media widgets
- Footer information

This allows testing of the full content extraction and filtering pipeline.

## Future Enhancements

Potential improvements to explore:

1. **Machine Learning**: Add ML-based content scoring
2. **Language Detection**: Multi-language content support
3. **Image Processing**: Extract and analyze article images  
4. **Schema.org**: Enhanced metadata extraction
5. **Custom Rules**: User-configurable extraction rules
6. **Performance**: Further optimization for large documents

---

This demonstrates Lambda Script's capability to implement sophisticated document processing algorithms while maintaining clean, functional code structure.

## Interactive Tetris

Run `./lambda.exe demo tetris` for a native, interactive Tetris
game written in Lambda. See [TETRIS.md](tetris/TETRIS.md) for controls and test commands.
