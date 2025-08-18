// Mozilla Readability-inspired content extraction for Lambda Script
// Exports transform() function to extract readable content from parsed HTML

// Content scoring weights (based on Mozilla Readability)
let SCORE_WEIGHTS = {
    // Positive indicators
    article: 25,
    main: 25,
    content: 15,
    post: 15,
    entry: 10,
    text: 10,
    story: 10,
    
    // Negative indicators  
    nav: -25,
    footer: -25,
    sidebar: -25,
    aside: -25,
    ad: -15,
    advertisement: -15,
    banner: -15,
    comment: -15,
    social: -10,
    share: -10,
    popup: -10,
    modal: -10
};

// Common content indicators in class names and IDs
let CONTENT_INDICATORS = [
    "article", "content", "entry", "main", "page", "post", "text", "story", 
    "body", "description", "summary", "detail", "full"
];

let NEGATIVE_INDICATORS = [
    "nav", "navigation", "menu", "sidebar", "aside", "footer", "header",
    "ad", "ads", "advert", "advertisement", "banner", "promo", "sponsored",
    "comment", "comments", "discuss", "social", "share", "sharing", "widget",
    "popup", "modal", "overlay", "tool", "meta", "breadcrumb", "pagination",
    "related", "recommend", "tag", "tags", "category", "author-bio"
];

// HTML tags that typically contain content vs navigation/metadata
let CONTENT_TAGS = ["p", "div", "article", "section", "main", "h1", "h2", "h3", "h4", "h5", "h6"];
let NEGATIVE_TAGS = ["nav", "footer", "aside", "header", "form", "script", "style", "noscript"];

// Minimum text length thresholds
let MIN_CONTENT_LENGTH = 25;
let MIN_PARAGRAPH_LENGTH = 25;

// Score an element based on its characteristics
fn score_element(element) {
    if (not element) { return 0 }
    
    let score = 0;
    let tag_name = if (element.tag) element.tag else "";
    let class_attr = if (element.class) element.class else "";
    let id_attr = if (element.id) element.id else "";
    
    // Base score by tag type
    if (tag_name == "article") { score = score + 25 }
    else if (tag_name == "main") { score = score + 25 }
    else if (tag_name == "section") { score = score + 15 }
    else if (tag_name == "div") { score = score + 5 }
    else if (tag_name in ["p", "h1", "h2", "h3", "h4", "h5", "h6"]) { score = score + 10 }
    else if (tag_name in NEGATIVE_TAGS) { score = score - 25 }
    
    // Score based on class names
    let classes = class_attr.split(" ");
    for class_name in classes {
        let lower_class = class_name.lower();
        if (lower_class in CONTENT_INDICATORS) { score = score + 10 }
        else if (lower_class in NEGATIVE_INDICATORS) { score = score - 15 }
        
        // Check for partial matches
        for indicator in CONTENT_INDICATORS {
            if (lower_class.contains(indicator)) { score = score + 5 }
        }
        for indicator in NEGATIVE_INDICATORS {
            if (lower_class.contains(indicator)) { score = score - 10 }
        }
    }
    
    // Score based on ID
    let lower_id = id_attr.lower();
    if (lower_id in CONTENT_INDICATORS) { score = score + 15 }
    else if (lower_id in NEGATIVE_INDICATORS) { score = score - 20 }
    
    // Check for partial ID matches
    for indicator in CONTENT_INDICATORS {
        if (lower_id.contains(indicator)) { score = score + 8 }
    }
    for indicator in NEGATIVE_INDICATORS {
        if (lower_id.contains(indicator)) { score = score - 15 }
    }
    
    score
}

// Extract text content from element, including nested elements
fn extract_text_content(element) {
    if (not element) { return "" }
    
    if (element.type == "text") {
        return element.text
    }
    
    let text = "";
    if (element.children) {
        for child in element.children {
            text = text + extract_text_content(child)
        }
    }
    
    // Add space after block elements
    if (element.tag in ["p", "div", "h1", "h2", "h3", "h4", "h5", "h6", "section", "article"]) {
        text = text + " "
    }
    
    text.trim()
}

// Count words in text
fn count_words(text) {
    if (not text) { return 0 }
    len(text.split(" ").filter(word => word.trim().length > 0))
}

// Check if element contains substantial content
fn has_substantial_content(element) {
    let text = extract_text_content(element);
    let word_count = count_words(text);
    
    // Must have minimum word count
    if (word_count < 10) { return false }
    
    // Must have reasonable text-to-link ratio
    let links = count_links(element);
    let link_ratio = if (word_count > 0) links / word_count else 1;
    
    // Reject if too many links relative to content
    if (link_ratio > 0.3) { return false }
    
    true
}

// Count number of links in element
fn count_links(element) {
    if (not element) { return 0 }
    
    let count = 0;
    if (element.tag == "a") { count = count + 1 }
    
    if (element.children) {
        for child in element.children {
            count = count + count_links(child)
        }
    }
    
    count
}

// Find all candidate elements that might contain main content
fn find_content_candidates(parsed_html) {
    let candidates = [];
    
    fn traverse_element(element) {
        if (not element) { return }
        
        // Score this element
        let score = score_element(element);
        
        // Check if it has substantial content
        if (has_substantial_content(element)) {
            let text_content = extract_text_content(element);
            let word_count = count_words(text_content);
            
            // Boost score based on content length
            score = score + min(word_count / 10, 50);
            
            candidates.push({
                element: element,
                score: score,
                text_content: text_content,
                word_count: word_count,
                tag: if (element.tag) element.tag else "unknown"
            })
        }
        
        // Recursively process children
        if (element.children) {
            for child in element.children {
                traverse_element(child)
            }
        }
    }
    
    traverse_element(parsed_html);
    candidates
}

// Extract title from HTML
fn extract_title(parsed_html) {
    fn find_title(element) {
        if (not element) { return null }
        
        // Look for <title> tag in <head>
        if (element.tag == "title") {
            return extract_text_content(element).trim()
        }
        
        // Look for main heading
        if (element.tag == "h1") {
            let text = extract_text_content(element).trim();
            if (text.length > 10) { return text }
        }
        
        // Recursively search children
        if (element.children) {
            for child in element.children {
                let title = find_title(child);
                if (title) { return title }
            }
        }
        
        null
    }
    
    find_title(parsed_html)
}

fn find_metadata(element) {
    // Look for meta tags
    if (element.tag == "meta") {
        if (element.name == "author") {
            metadata.author = element.content
        }
        else if (element.name == "description") {
            metadata.description = element.content
        }
        else if (element.name == "date" or element.property == "article:published_time") {
            metadata.date = element.content
        }
    }
    
    // Look for time elements
    if (element.tag == "time") {
        let datetime = if (element.datetime) element.datetime else extract_text_content(element);
        if (not metadata.date) { metadata.date = datetime }
    }
    
    // Look for byline patterns
    if (element.class and element.class.contains("byline")) {
        let text = extract_text_content(element);
        if (text.contains("By ") and not metadata.author) {
            metadata.author = text.replace("By ", "").trim()
        }
    }
    
    // Recursively search children
    if (element.children) {
        for child in element.children {
            find_metadata(child)
        }
    }
}

// Extract meta information (author, date, description)
fn extract_metadata(parsed_html) {
    let metadata = {
        author: null,
        date: null,
        description: null
    };
    
    find_metadata(parsed_html);
    metadata
}

// Clean up the selected content by removing unwanted elements
fn clean_content(element) {
    if (not element) { return null }
    
    // Skip elements that are likely not content
    if (element.tag in NEGATIVE_TAGS) { return null }
    
    let tag = if (element.tag) element.tag else "";
    let class_attr = if (element.class) element.class else "";
    let id_attr = if (element.id) element.id else "";
    
    // Skip based on class/ID patterns
    let combined_attrs = (class_attr + " " + id_attr).lower();
    for indicator in NEGATIVE_INDICATORS {
        if (combined_attrs.contains(indicator)) { return null }
    }
    
    // For text nodes, return as-is if long enough
    if (element.type == "text") {
        let text = element.text.trim();
        return if (text.length >= MIN_PARAGRAPH_LENGTH) element else null
    }
    
    // Clean children recursively
    let cleaned_children = [];
    if (element.children) {
        for child in element.children {
            let cleaned = clean_content(child);
            if (cleaned) { cleaned_children.push(cleaned) }
        }
    }
    
    // Return cleaned element if it has valid children or is a content tag
    if (cleaned_children.length > 0 or tag in CONTENT_TAGS) {
        {
            tag: tag,
            class: element.class,
            id: element.id,
            children: cleaned_children,
            type: element.type,
            text: if (element.text) element.text else null
        }
    } else {
        null
    }
}

// Main transform function - extracts readable content from parsed HTML
pub fn transform(parsed_html) {
    if (not parsed_html) {
        return error("No HTML content provided")
    }
    
    // Extract basic metadata
    let title = extract_title(parsed_html);
    let metadata = extract_metadata(parsed_html);
    
    // Find content candidates
    let candidates = find_content_candidates(parsed_html);
    
    if (candidates.length == 0) {
        return {
            title: title,
            author: metadata.author,
            date: metadata.date,
            description: metadata.description,
            content: null,
            text_content: "",
            word_count: 0,
            success: false,
            message: "No suitable content found"
        }
    }
    
    // Sort candidates by score (highest first)
    let sorted_candidates = candidates.sort((a, b) => b.score - a.score);
    let best_candidate = sorted_candidates[0];
    
    // Clean the selected content
    let cleaned_content = clean_content(best_candidate.element);
    
    // Extract final text content
    let final_text = if (cleaned_content) extract_text_content(cleaned_content) else "";
    let final_word_count = count_words(final_text);
    
    {
        title: title,
        author: metadata.author,
        date: metadata.date,
        description: metadata.description,
        content: cleaned_content,
        text_content: final_text,
        word_count: final_word_count,
        score: best_candidate.score,
        success: true,
        message: "Content extracted successfully",
        debug: {
            candidates_found: candidates.length,
            selected_tag: best_candidate.tag,
            selected_score: best_candidate.score
        }
    }
}

// Helper function to format readable output as markdown
pub fn format_readable_markdown(readable_result) {
    if (not readable_result.success) {
        return "# Error\n\n" + readable_result.message
    }
    
    let output = "";
    
    // Add title
    if (readable_result.title) {
        output = output + "# " + readable_result.title + "\n\n"
    }
    
    // Add metadata
    if (readable_result.author or readable_result.date) {
        if (readable_result.author) {
            output = output + "**Author:** " + readable_result.author + "\n"
        }
        if (readable_result.date) {
            output = output + "**Published:** " + readable_result.date + "\n"
        }
        output = output + "\n"
    }
    
    // Add description
    if (readable_result.description) {
        output = output + "*" + readable_result.description + "*\n\n"
    }
    
    // Add main content as text
    if (readable_result.text_content) {
        output = output + readable_result.text_content + "\n\n"
    }
    
    // Add statistics
    output = output + "---\n"
    output = output + "**Word count:** " + readable_result.word_count + "\n"
    output = output + "**Readability score:** " + readable_result.score + "\n"
    
    output
}
