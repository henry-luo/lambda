# Web Page Downloader

A Node.js script that downloads a static web page along with its CSS files, images, and web fonts.

## Features

- Downloads the main HTML page
- Downloads linked CSS stylesheets (including `@import` dependencies)
- Downloads embedded images (`<img src>`, `srcset`, inline `style` backgrounds)
- Downloads web fonts (`.woff`, `.woff2`, `.ttf`, `.otf`, `.eot`)
- Downloads favicon and touch icons
- Rewrites URLs in HTML and CSS to point to local files
- Uses safe ASCII-only filenames for all downloaded resources
- Handles HTTP redirects
- Skips JavaScript files (as requested)

## Usage

```bash
node download-page.js <url> <output-dir>
```

## Example

```bash
# Download example.com to ./output directory
node download-page.js https://example.com ./output

# Download a specific page
node download-page.js https://news.ycombinator.com ./hacker-news
```

## Output Structure

```
<output-dir>/
├── index.html          # Main HTML (rewritten to use local resources)
└── res/                # All resources (CSS, images, fonts)
    ├── style.css
    ├── logo.png
    ├── font.woff2
    └── ...
```

## Limitations

- Only downloads static resources (no JavaScript execution)
- Does not handle dynamically loaded content
- May not work with pages that require authentication
- Some resources behind CORS or with anti-hotlinking may fail to download

## Requirements

- Node.js 14 or later (uses built-in modules only, no npm install needed)
