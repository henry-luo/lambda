// Unit tests for readability.ls
// Tests individual functions and parsing without file dependencies
//
// Run: ./lambda.exe test/readable/unit_test.ls

import readability: .utils.readability;

// ============================================
// Test HTML Samples (using string concatenation for multi-line)
// ============================================

let SIMPLE_ARTICLE = 
    "<!DOCTYPE html>" ++
    "<html lang=\"en\">" ++
    "<head>" ++
    "    <meta charset=\"UTF-8\">" ++
    "    <title>Test Article Title</title>" ++
    "    <meta name=\"description\" content=\"This is a test article description.\">" ++
    "    <meta name=\"author\" content=\"John Doe\">" ++
    "</head>" ++
    "<body>" ++
    "    <header>" ++
    "        <nav>Navigation menu here</nav>" ++
    "    </header>" ++
    "    <main>" ++
    "        <article>" ++
    "            <h1>Test Article Title</h1>" ++
    "            <p class=\"byline\">By John Doe</p>" ++
    "            <p>This is the first paragraph of the article. It contains important " ++
    "            information that readers want to see. The content should be extracted " ++
    "            properly by the readability algorithm.</p>" ++
    "            <p>This is the second paragraph with more content. It provides additional " ++
    "            details and context that enhance the reader experience.</p>" ++
    "            <p>The third paragraph concludes our test article with a summary of " ++
    "            the key points discussed above.</p>" ++
    "        </article>" ++
    "    </main>" ++
    "    <footer>" ++
    "        <p>Copyright 2024</p>" ++
    "    </footer>" ++
    "</body>" ++
    "</html>";

let ARTICLE_WITH_METADATA = 
    "<!DOCTYPE html>" ++
    "<html lang=\"en-US\">" ++
    "<head>" ++
    "    <title>News Article | Example News</title>" ++
    "    <meta property=\"og:title\" content=\"Breaking News: Important Event\">" ++
    "    <meta property=\"og:description\" content=\"Details about the important event.\">" ++
    "    <meta property=\"og:site_name\" content=\"Example News\">" ++
    "    <meta property=\"article:author\" content=\"Jane Smith\">" ++
    "    <meta property=\"article:published_time\" content=\"2024-01-15T10:00:00Z\">" ++
    "</head>" ++
    "<body>" ++
    "    <article>" ++
    "        <h1>Breaking News: Important Event</h1>" ++
    "        <p>The important event happened today. Many people were affected by this " ++
    "        development. Experts are analyzing the situation.</p>" ++
    "        <p>More details emerged as the day progressed. Officials released statements " ++
    "        addressing public concerns about the event.</p>" ++
    "    </article>" ++
    "</body>" ++
    "</html>";

let ARTICLE_WITH_NOISE = 
    "<!DOCTYPE html>" ++
    "<html>" ++
    "<head><title>Article with Noise</title></head>" ++
    "<body>" ++
    "    <div class=\"header\">" ++
    "        <div class=\"social-share\">Share on Twitter | Share on Facebook</div>" ++
    "    </div>" ++
    "    <div class=\"sidebar\">" ++
    "        <div class=\"ad-banner\">Advertisement</div>" ++
    "        <div class=\"related-posts\">Related posts here</div>" ++
    "    </div>" ++
    "    <div class=\"content main-article\">" ++
    "        <h1>The Main Article</h1>" ++
    "        <p>This is the actual article content that should be extracted. It contains " ++
    "        meaningful text that readers came to see. The algorithm should identify this " ++
    "        as the main content.</p>" ++
    "        <p>Additional paragraphs provide more context and information. This helps " ++
    "        ensure the content scoring works correctly.</p>" ++
    "        <p>Third paragraph with more substantial content to meet length requirements " ++
    "        for proper extraction.</p>" ++
    "    </div>" ++
    "    <div class=\"footer\">" ++
    "        <div class=\"comments\">Comments section</div>" ++
    "        <div class=\"sponsors\">Sponsored content</div>" ++
    "    </div>" ++
    "</body>" ++
    "</html>";

// ============================================
// Test Functions
// ============================================

fn test_parse_simple() {
    let result = readability.parse(SIMPLE_ARTICLE);
    [
        "=== Test: parse_simple ===",
        ["title", result.title],
        ["lang", result.lang],
        ["has_content", result.content != null],
        ["textContent_length", result.length > 0]
    ]
}

fn test_metadata_extraction() {
    let result = readability.parse(ARTICLE_WITH_METADATA);
    [
        "=== Test: metadata_extraction ===",
        ["title_from_og", result.title],
        ["lang", result.lang],
        ["siteName", result.siteName],
        ["excerpt", result.excerpt],
        ["publishedTime", result.publishedTime]
    ]
}

fn test_noise_filtering() {
    let result = readability.parse(ARTICLE_WITH_NOISE);
    [
        "=== Test: noise_filtering ===",
        ["title", result.title],
        ["has_content", result.content != null],
        ["textContent_length", result.length]
    ]
}

fn test_is_readable() {
    [
        "=== Test: is_readable ===",
        ["simple_readable", readability.is_readable(SIMPLE_ARTICLE, null, null)],
        ["metadata_readable", readability.is_readable(ARTICLE_WITH_METADATA, null, null)],
        ["noise_readable", readability.is_readable(ARTICLE_WITH_NOISE, null, null)]
    ]
}

fn test_title_function() {
    let doc = input(SIMPLE_ARTICLE, 'html);
    [
        "=== Test: title function ===",
        ["title", readability.title(doc)]
    ]
}

fn test_text_function() {
    let doc = input(SIMPLE_ARTICLE, 'html);
    let text = readability.text(doc);
    [
        "=== Test: text function ===",
        ["has_text", len(text) > 0],
        ["text_length", len(text)]
    ]
}

// ============================================
// Run All Tests
// ============================================

[
    "=== Readability Unit Tests ===",
    test_parse_simple(),
    test_metadata_extraction(),
    test_noise_filtering(),
    test_is_readable(),
    test_title_function(),
    test_text_function(),
    "=== All Tests Complete ==="
]
