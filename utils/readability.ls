// Readability - Extract main content from HTML documents
// Port of Mozilla Readability.js concepts to Lambda Script
//
// Usage:
//   import readability: .utils.readability
//   
//   // Parse HTML file and extract readable content
//   let result = readability.parse(@./article.html)
//   
//   // Result contains:
//   //   title: string?        - Article title
//   //   byline: string?       - Author info
//   //   content: element?     - Extracted content element (can be saved)
//   //   textContent: string   - Clean text without HTML
//   //   length: int           - Text length
//   //   excerpt: string?      - Article excerpt/summary
//   //   siteName: string?     - Site name
//   //   lang: string?         - Language code
//   //   dir: string?          - Text direction (ltr/rtl)
//   //   publishedTime: string? - Publication timestamp
//
// Saving extracted content:
//   readability.parse_and_save(@./input.html, @./output.html)
//
// Quick metadata extraction:
//   let title = readability.title(@./article.html)
//   let lang = readability.lang(@./article.html)
//
// Note: Lambda's input() function works with file paths, not raw string content.
// Use parse(@./file.html) to parse HTML files directly.
//
// Key Design Principle:
//   This module follows Lambda's pure functional design - it DOES NOT modify
//   the input document. Instead, it produces a NEW clean document that can
//   be serialized and saved independently. The result.content field contains
//   the extracted article element, while result.textContent provides clean
//   text without any HTML markup.
//
// Based on Mozilla Readability.js (https://github.com/mozilla/readability)

// ============================================
// Configuration Constants
// ============================================

let DEFAULT_CHAR_THRESHOLD = 500
let DEFAULT_N_TOP_CANDIDATES = 5
let MIN_CONTENT_LENGTH = 140
let MIN_SCORE = 20

// Pattern lists for candidate classification
let UNLIKELY_CANDIDATES = [
    "ad", "ai2html", "banner", "breadcrumbs", "combx", "comment", "community",
    "cover-wrap", "disqus", "extra", "footer", "gdpr", "header", "legends",
    "menu", "related", "remark", "replies", "rss", "shoutbox", "sidebar",
    "skyscraper", "social", "sponsor", "supplemental", "ad-break", "agegate",
    "pagination", "pager", "popup", "yom-remote"
]

let OK_MAYBE_CANDIDATE = ["and", "article", "body", "column", "content", "main", "mathjax", "shadow"]

let POSITIVE_PATTERNS = [
    "article", "body", "content", "entry", "hentry", "h-entry", "main",
    "page", "pagination", "post", "text", "blog", "story"
]

let NEGATIVE_PATTERNS = [
    "ad", "hidden", "hid", "banner", "combx", "comment", "com-", "contact",
    "footer", "gdpr", "masthead", "media", "meta", "outbrain", "promo",
    "related", "scroll", "share", "shoutbox", "sidebar", "skyscraper",
    "sponsor", "shopping", "tags", "widget"
]

let BYLINE_PATTERNS = ["byline", "author", "dateline", "writtenby", "p-author"]

let UNLIKELY_ROLES = ["menu", "menubar", "complementary", "navigation", "alert", "alertdialog", "dialog"]

// Tags that should be scored
let TAGS_TO_SCORE = ['section', 'h2', 'h3', 'h4', 'h5', 'h6', 'p', 'td', 'pre']

// Block-level elements that can contain phrasing content
let DIV_TO_P_ELEMS = ['blockquote', 'dl', 'div', 'img', 'ol', 'p', 'pre', 'table', 'ul']

// Video site patterns (simplified)
let VIDEO_SITES = ["youtube", "vimeo", "dailymotion", "twitch", "bilibili"]

// ============================================
// String Helper Functions
// ============================================

// Check if string matches any pattern in list
fn matches_any(str, patterns) =>
    if (str == null or len(patterns) == 0) false
    else (
        let lower_str = lower(str),
        let found = [for (p in patterns where contains(lower_str, p)) true],
        len(found) > 0
    )

// Normalize whitespace
fn normalize_whitespace(str) =>
    if (str == null) ""
    else (
        let trimmed = trim(str),
        let parts = split(trimmed, " "),
        let non_empty = [for (p in parts where len(trim(p)) > 0) trim(p)],
        str_join(non_empty, " ")
    )

// Word count approximation
fn word_count(str) =>
    if (str == null or len(trim(str)) == 0) 0
    else (
        let parts = split(trim(str), " "),
        let non_empty = [for (p in parts where len(trim(p)) > 0) true],
        len(non_empty)
    )

// Count commas in text (used for scoring)
fn count_commas(str) =>
    if (str == null) 0
    else (
        let parts = split(str, ","),
        len(parts) - 1
    )

// ============================================
// Element Helper Functions
// ============================================

// Get tag name as symbol
fn get_tag(elem) =>
    if (elem is element) elem.tag
    else 'unknown'

// Check if element has specific tag
fn has_tag(elem, tag_name) =>
    get_tag(elem) == symbol(lower(tag_name))

// Get attribute value or default
fn get_attr(elem, name, default_val) =>
    if (not (elem is element)) default_val
    else (
        let val = elem[symbol(name)],
        if (val != null) string(val) else default_val
    )

// Get class attribute
fn get_class(elem) =>
    get_attr(elem, "class", "")

// Get id attribute
fn get_id(elem) =>
    get_attr(elem, "id", "")

// Get match string (class + " " + id)
fn get_match_string(elem) =>
    get_class(elem) ++ " " ++ get_id(elem)

// Check if element has any of the given tags
fn has_any_tag(elem, tag_list) =>
    if (not (elem is element)) false
    else (
        let elem_tag = get_tag(elem),
        let matches = [for (t in tag_list where elem_tag == symbol(lower(t))) true],
        len(matches) > 0
    )

// Get element's role attribute
fn get_role(elem) =>
    get_attr(elem, "role", "")

// Check if node has ancestor with specific tag
fn has_ancestor_tag(elem, tag_name, max_depth) => (
    let target = symbol(lower(tag_name)),
    let depth = if (max_depth != null) max_depth else 3,
    
    // helper to check parent chain
    // since we can't do true recursion with parent refs easily,
    // we'll implement this when we have parent tracking
    // for now return false (conservative)
    false
)

// ============================================
// Text Content Extraction
// ============================================

// Get text content from element (recursive)
fn get_text(elem) =>
    if (elem is string) elem
    else if (elem == null) ""
    else if (not (elem is element)) string(elem)
    else (
        let n = len(elem),
        if (n == 0) ""
        else (
            let parts = [for (i in 0 to n - 1) get_text(elem[i])],
            str_join(parts, "")
        )
    )

// Get inner text with normalized whitespace
fn get_inner_text(elem, do_normalize) => (
    let text = get_text(elem),
    if (do_normalize) normalize_whitespace(text) else trim(text)
)

// ============================================
// Element Search Functions
// ============================================

// Find first child with matching tag
fn find_child(elem, tag_name) =>
    if (elem == null or not (elem is element)) null
    else (
        let n = len(elem),
        if (n == 0) null
        else (
            let target = symbol(lower(tag_name)),
            let filtered = [for (i in 0 to n - 1, 
                                let child = elem[i]
                                where child is element and get_tag(child) == target) child],
            if (len(filtered) > 0) filtered[0] else null
        )
    )

// Deep recursive search for element with tag
fn find_deep(elem, tag_name) =>
    if (elem == null) null
    else if (not (elem is element)) null
    else if (has_tag(elem, tag_name)) elem
    else (
        let n = len(elem),
        if (n == 0) null
        else (
            let results = [for (i in 0 to n - 1) find_deep(elem[i], tag_name)],
            let found = [for (r in results where r is element) r],
            if (len(found) > 0) found[0] else null
        )
    )

// Helper function for find_all - recursive collect
fn find_all_helper(e, target) =>
    if (e == null or not (e is element)) []
    else (
        let matches = if (get_tag(e) == target) [e] else [],
        let n = len(e),
        let child_matches = [for (i in 0 to n - 1) for (m in find_all_helper(e[i], target)) m],
        matches ++ child_matches
    )

// Collect all matching elements
fn find_all(elem, tag_name) => (
    let target = symbol(lower(tag_name)),
    find_all_helper(elem, target)
)

// ============================================
// Scoring Functions
// ============================================

// Get class/id weight
fn get_class_weight(elem) => (
    let match_string = get_match_string(elem),
    let w0 = 0,
    let w1 = if (matches_any(match_string, NEGATIVE_PATTERNS)) w0 - 25 else w0,
    let w2 = if (matches_any(match_string, POSITIVE_PATTERNS)) w1 + 25 else w1,
    w2
)

// Calculate link density (ratio of link text to total text)
fn get_link_density(elem) => (
    let text_length = len(get_inner_text(elem, true)),
    if (text_length == 0) 0.0
    else (
        let links = find_all(elem, "a"),
        let link_lengths = [for (link in links) len(get_inner_text(link, true))],
        let link_length = sum(link_lengths),
        (link_length * 1.0) / (text_length * 1.0)
    )
)

// Initialize score for an element based on tag
fn get_initial_score(elem) => (
    let tag = get_tag(elem),
    if (tag == 'div') 5
    else if (tag == 'pre' or tag == 'td' or tag == 'blockquote') 3
    else if (tag == 'address' or tag == 'ol' or tag == 'ul' or 
             tag == 'dl' or tag == 'dd' or tag == 'dt' or
             tag == 'li' or tag == 'form') -3
    else if (tag == 'h1' or tag == 'h2' or tag == 'h3' or
             tag == 'h4' or tag == 'h5' or tag == 'h6' or tag == 'th') -5
    else 0
)

// Calculate content score for an element
fn calculate_content_score(elem) => (
    let text = get_inner_text(elem, true),
    let text_len = len(text),
    
    // Start with class weight
    let score0 = get_class_weight(elem),
    
    // Add initial score based on tag
    let score1 = score0 + get_initial_score(elem),
    
    // Add points for commas (indicates sentences)
    let comma_count = count_commas(text),
    let score2 = score1 + comma_count,
    
    // Add points for text length (diminishing returns after 200 chars)
    let len_score = min(floor(text_len / 100), 3),
    let score3 = score2 + len_score,
    
    // Penalize high link density
    let link_density = get_link_density(elem),
    let score4 = score3 - (score3 * link_density),
    
    score4
)

// Check if element is probably visible
fn is_probably_visible(elem) =>
    if (not (elem is element)) false
    else (
        let style = get_attr(elem, "style", ""),
        let hidden = get_attr(elem, "hidden", null),
        let aria_hidden = get_attr(elem, "aria-hidden", ""),
        
        not (contains(lower(style), "display:none") or
             contains(lower(style), "display: none") or
             contains(lower(style), "visibility:hidden") or
             contains(lower(style), "visibility: hidden") or
             hidden != null or
             aria_hidden == "true")
    )

// Check if unlikely candidate
fn is_unlikely_candidate(elem) => (
    let match_string = lower(get_match_string(elem)),
    let is_unlikely = matches_any(match_string, UNLIKELY_CANDIDATES),
    let is_maybe_ok = matches_any(match_string, OK_MAYBE_CANDIDATE),
    // Also check role
    let role = get_role(elem),
    let has_unlikely_role = [for (r in UNLIKELY_ROLES where r == role) true],
    (is_unlikely and not is_maybe_ok) or len(has_unlikely_role) > 0
)

// Check if element is valid byline
fn is_valid_byline(elem) => (
    let match_string = lower(get_match_string(elem)),
    let rel = lower(get_attr(elem, "rel", "")),
    let itemprop = lower(get_attr(elem, "itemprop", "")),
    
    let has_byline_class = matches_any(match_string, BYLINE_PATTERNS),
    let has_author_rel = rel == "author",
    let has_author_itemprop = contains(itemprop, "author"),
    
    let text_len = len(trim(get_text(elem))),
    
    (has_byline_class or has_author_rel or has_author_itemprop) and
    text_len > 0 and text_len < 100
)

// Check if element has content
fn has_content(elem) =>
    if (not (elem is element)) false
    else (
        let text = trim(get_text(elem)),
        let images = find_all(elem, "img"),
        let videos = find_all(elem, "video"),
        let iframes = find_all(elem, "iframe"),
        
        len(text) > 0 or len(images) > 0 or len(videos) > 0 or len(iframes) > 0
    )

// ============================================
// Content Cleaning & Document Building
// ============================================
// These functions create NEW elements rather than modifying existing ones,
// following Lambda's pure functional design.

// Tags to remove entirely (including contents) - as strings, converted to symbols when checking
let REMOVE_TAGS = ["script", "style", "noscript", "iframe", "form", "button", "input", "select", "textarea", "nav", "aside", "header", "footer", "template"]

// Tags that are just containers - unwrap their contents
let UNWRAP_TAGS = ["span", "font", "center"]

// Attributes to preserve (whitelist approach for cleaner output)
let PRESERVE_ATTRS = ["href", "src", "alt", "title", "width", "height", "srcset", "sizes", "colspan", "rowspan", "headers", "datetime", "cite", "lang", "dir"]

// Tags that become empty and should be removed
let CLEANUP_TAGS = ["div", "section", "article", "p"]

// Check if tag should be removed entirely
fn should_remove_tag(tag) =>
    string(tag) in REMOVE_TAGS

// Check if tag should be unwrapped (keep contents, remove tag)
fn should_unwrap_tag(tag) =>
    string(tag) in UNWRAP_TAGS

// Check if attribute should be preserved
fn should_preserve_attr(attr_name) =>
    lower(string(attr_name)) in PRESERVE_ATTRS

// Check if element looks like an ad or non-content element
fn is_ad_or_noise(elem) => (
    let match_string = lower(get_match_string(elem)),
    let ad_patterns = ["ad", "advertisement", "sponsor", "promo", "banner", 
                       "social", "share", "related", "recommended", "newsletter",
                       "subscription", "signup", "popup", "modal"],
    matches_any(match_string, ad_patterns)
)

// Clean and rebuild a single element (recursive, produces NEW element)
// Returns: cleaned element tree, list of children for unwrapped tags, or null
fn clean_element(elem) =>
    if (elem == null) null
    else if (elem is string) (
        // Text node - keep as-is if not empty
        let trimmed = trim(elem),
        if (len(trimmed) == 0) null else elem
    )
    else if (not (elem is element)) string(elem)
    else (
        let tag = get_tag(elem),
        let tag_str = string(tag),
        
        // Skip elements that should be removed entirely
        if (should_remove_tag(tag)) null
        else if (is_ad_or_noise(elem)) null
        else (
            // Clean children first
            let n = len(elem),
            let cleaned_children = [for (i in 0 to n - 1) clean_element(elem[i])],
            let non_null_children = [for (c in cleaned_children where c != null) c],
            
            // If tag should be unwrapped, return children as text
            if (should_unwrap_tag(tag)) (
                if (len(non_null_children) == 0) null
                else if (len(non_null_children) == 1) non_null_children[0]
                else str_join([for (c in non_null_children) (if (c is string) c else get_text(c))], " ")
            )
            // Check if now empty after cleaning
            else if (len(non_null_children) == 0 and tag_str in CLEANUP_TAGS) null
            // Return the original element
            else elem
        )
    )

// Get cleaned text content from an element tree (recursive)
fn get_clean_text(elem) =>
    if (elem == null) ""
    else if (elem is string) elem
    else if (not (elem is element)) string(elem)
    else (
        let tag = get_tag(elem),
        if (should_remove_tag(tag)) ""
        else if (is_ad_or_noise(elem)) ""
        else (
            let n = len(elem),
            let parts = [for (i in 0 to n - 1) get_clean_text(elem[i])],
            str_join(parts, " ")
        )
    )

// ============================================
// Metadata Extraction
// ============================================

// Extract title from document
fn extract_title(doc) => (
    let head = find_deep(doc, "head"),
    let title_elem = if (head != null) find_deep(head, "title") else null,
    
    let orig_title = if (title_elem != null) trim(get_text(title_elem)) else "",
    
    // check for separators and extract just article title
    let has_sep = contains(orig_title, " | ") or contains(orig_title, " - ") or
                  contains(orig_title, " — ") or contains(orig_title, " » "),
    
    let title_from_sep = if (has_sep) (
        let parts = if (contains(orig_title, " | ")) split(orig_title, " | ")
                    else if (contains(orig_title, " - ")) split(orig_title, " - ")
                    else if (contains(orig_title, " — ")) split(orig_title, " — ")
                    else split(orig_title, " » "),
        
        if (len(parts) > 1) (
            let first = trim(parts[0]),
            let last = trim(parts[len(parts) - 1]),
            if (len(first) > len(last)) first else last
        )
        else orig_title
    )
    else orig_title,
    
    // if title too short, try h1
    let final_title = if (len(title_from_sep) < 15 or len(title_from_sep) > 150) (
        let h1s = find_all(doc, "h1"),
        if (len(h1s) == 1) get_inner_text(h1s[0], true)
        else title_from_sep
    )
    else title_from_sep,
    
    normalize_whitespace(final_title)
)

// Helper for checking name match - handles null safely
fn meta_name_matches(meta_name, query_name) =>
    if (query_name == "" or query_name == null) false
    else if (meta_name == "" or meta_name == null) false
    else lower(meta_name) == lower(query_name)

// Helper for checking property match - handles null safely
fn meta_property_matches(meta_property, query_property) =>
    if (query_property == "" or query_property == null) false
    else if (meta_property == "" or meta_property == null) false
    else lower(meta_property) == lower(query_property)

// Get meta content by name or property
// Note: Uses separate condition computation to work around Lambda's
// compound boolean evaluation issues in where clauses
fn get_meta_content(doc, name, property) => (
    let metas = find_all(doc, "meta"),
    // Compute all conditions as separate values, then filter
    let all_data = [for (meta in metas,
                         let meta_name = get_attr(meta, "name", ""),
                         let meta_property = get_attr(meta, "property", ""),
                         let content = get_attr(meta, "content", ""),
                         let name_match = meta_name_matches(meta_name, name),
                         let prop_match = meta_property_matches(meta_property, property),
                         let has_content = content != "" and content != null,
                         let should_include = has_content and (name_match or prop_match))
                    {content: content, match: should_include}],
    let matched = [for (d in all_data where d.match == true) d.content],
    if (len(matched) > 0) matched[0] else null
)

// Extract JSON-LD metadata
// NOTE: JSON-LD parsing disabled - input() requires file paths, not string content
// TODO: Re-enable when a parse_json(string) function is available
fn get_json_ld(doc) => null

// Extract article metadata
fn extract_metadata(doc, json_ld_param) => (
    let jld0 = if (json_ld_param != null) json_ld_param else get_json_ld(doc),
    let jld = if (jld0 != null) jld0 else {},
    
    let title = if (jld.title != null) jld.title
                else (
                    let og_title = get_meta_content(doc, "", "og:title"),
                    if (og_title != null) og_title
                    else (
                        let tw_title = get_meta_content(doc, "", "twitter:title"),
                        if (tw_title != null) tw_title
                        else extract_title(doc)
                    )
                ),
    
    let byline = if (jld.byline != null) jld.byline
                 else (
                     let author = get_meta_content(doc, "author", ""),
                     if (author != null) author
                     else get_meta_content(doc, "", "article:author")
                 ),
    
    let excerpt = if (jld.excerpt != null) jld.excerpt
                  else (
                      let desc = get_meta_content(doc, "description", ""),
                      if (desc != null) desc
                      else (
                          let og_desc = get_meta_content(doc, "", "og:description"),
                          if (og_desc != null) og_desc
                          else get_meta_content(doc, "", "twitter:description")
                      )
                  ),
    
    let siteName = if (jld.siteName != null) jld.siteName
                   else get_meta_content(doc, "", "og:site_name"),
    
    let publishedTime = if (jld.publishedTime != null) jld.publishedTime
                        else get_meta_content(doc, "", "article:published_time"),
    
    {
        title: title,
        byline: byline,
        excerpt: excerpt,
        siteName: siteName,
        publishedTime: publishedTime
    }
)

// Get document language
fn get_lang(doc) => (
    let html = find_deep(doc, "html"),
    if (html != null) get_attr(html, "lang", null) else null
)

// Get text direction
fn get_dir(doc) => (
    let html = find_deep(doc, "html"),
    let body = find_deep(doc, "body"),
    let html_dir = if (html != null) get_attr(html, "dir", null) else null,
    let body_dir = if (body != null) get_attr(body, "dir", null) else null,
    if (html_dir != null) html_dir else body_dir
)

// ============================================
// Content Selection
// ============================================

// Score all paragraphs and their ancestors
fn score_paragraphs(body) => (
    let ps = find_all(body, "p"),
    let pres = find_all(body, "pre"),
    let tds = find_all(body, "td"),
    let all_elems = ps ++ pres ++ tds,
    
    // Score each element
    let scored = [for (elem in all_elems,
                       let is_visible = is_probably_visible(elem),
                       let is_likely = not is_unlikely_candidate(elem),
                       let inner_text = get_inner_text(elem, true),
                       let text_len = len(inner_text)
                       where is_visible and is_likely and text_len >= 25)
                  {
                      elem: elem,
                      text: inner_text,
                      score: calculate_content_score(elem)
                  }],
    scored
)

// Find the best content candidate using multi-pass scoring
fn find_best_candidate(doc) => (
    let body = find_deep(doc, "body"),
    if (body == null) null
    else (
        // First, check for article or main semantic elements
        let articles = find_all(body, "article"),
        let visible_articles = [for (a in articles where is_probably_visible(a) and has_content(a)) a],
        
        // If there's exactly one article with substantial content, use it
        if (len(visible_articles) == 1) visible_articles[0]
        else (
            // Check for main element
            let main_elem = find_deep(body, "main"),
            if (main_elem != null and is_probably_visible(main_elem) and has_content(main_elem)) main_elem
            else (
                // Score all candidate elements
                let divs = find_all(body, "div"),
                let sections = find_all(body, "section"),
                let all_candidates = divs ++ sections,
                
                // Filter and score candidates
                let scored = [for (cand in all_candidates,
                                   let is_visible = is_probably_visible(cand),
                                   let is_likely = not is_unlikely_candidate(cand),
                                   let text = get_inner_text(cand, true),
                                   let text_len = len(text)
                                   where is_visible and is_likely and text_len >= DEFAULT_CHAR_THRESHOLD)
                              {
                                  elem: cand,
                                  text_len: text_len,
                                  score: calculate_content_score(cand)
                              }],
                
                if (len(scored) == 0) body
                else (
                    // Sort by score descending
                    let sorted = for (s in scored order by s.score desc) s,
                    let top = sorted[0],
                    
                    // Check if there are close competitors
                    let top_score = top.score,
                    let threshold = top_score * 0.75,
                    let close_candidates = [for (s in sorted where s.score >= threshold) s],
                    
                    // If multiple close candidates, prefer one with more text
                    if (len(close_candidates) > 1) (
                        let by_text = for (c in close_candidates order by c.text_len desc) c,
                        by_text[0].elem
                    )
                    else top.elem
                )
            )
        )
    )
)

// ============================================
// Public API Functions
// ============================================

/// Parse HTML file and extract readable content.
/// Returns a result map with cleaned content, or propagates error if file can't be read.
/// The 'content' field contains the extracted article content element.
/// The 'textContent' field contains clean text without HTML tags.
/// 
/// Usage: readability.parse(@./article.html)
pub fn parse(file_path) map^ => parse_doc(input(file_path, 'html')?)

/// Parse HTML document object.
/// Produces clean content - follows Lambda's functional design.
pub fn parse_doc(doc) => (
    let json_ld = get_json_ld(doc),
    let metadata = extract_metadata(doc, json_ld),
    let article_content = find_best_candidate(doc),
    let lang = get_lang(doc),
    let dir = get_dir(doc),
    
    if (article_content == null)
        {
            title: metadata.title,
            byline: metadata.byline,
            dir: dir,
            content: null,
            textContent: null,
            length: 0,
            excerpt: metadata.excerpt,
            siteName: metadata.siteName,
            lang: lang,
            publishedTime: metadata.publishedTime
        }
    else (
        // Try to extract byline from article if not in metadata
        let article_byline = if (metadata.byline != null) metadata.byline
                             else find_byline_in_article(article_content),
        
        let excerpt = if (metadata.excerpt != null) metadata.excerpt
                      else (
                          let ps = find_all(article_content, "p"),
                          if (len(ps) > 0) (
                              let first_text = get_inner_text(ps[0], true),
                              if (len(first_text) > 0) first_text else null
                          )
                          else null
                      ),
        
        // Get cleaned text content (no script/style/ad content)
        let text_content = get_clean_text(article_content),
        let normalized_text = normalize_whitespace(text_content),
        
        {
            title: metadata.title,
            byline: article_byline,
            dir: dir,
            content: article_content,      // The extracted content element
            textContent: normalized_text,  // Clean text without HTML
            length: len(normalized_text),
            excerpt: excerpt,
            siteName: metadata.siteName,
            lang: lang,
            publishedTime: metadata.publishedTime
        }
    )
)

// Helper to find byline within article content
fn find_byline_in_article(article) => (
    let all_elems = collect_all_elements(article),
    let byline_elems = [for (e in all_elems where is_valid_byline(e)) e],
    if (len(byline_elems) > 0) trim(get_text(byline_elems[0]))
    else null
)

// Helper to collect all elements recursively
fn collect_all_elements(elem) =>
    if (not (elem is element)) []
    else (
        let n = len(elem),
        let children = [for (i in 0 to n - 1, let child = elem[i] where child is element) child],
        let descendants = [for (c in children) for (d in collect_all_elements(c)) d],
        children ++ descendants
    )

/// Parse HTML file and extract readable content (alias for parse)
/// Deprecated: Use parse() directly
pub fn parse_file(file_path) map^ => parse(file_path)

/// Parse and save clean content to file.
/// Saves the extracted article content as HTML to the specified path.
/// Usage: readability.parse_and_save(@./input.html, @./output.html)
pub pn parse_and_save(input_path, output_path) map^ {
    var result^err = parse(input_path)
    if (err != null) { raise err }
    if (result.content != null) {
        var out^out_err = output(result.content, output_path, 'html')
        if (out_err != null) { raise out_err }
    }
    return result
}

/// Extract just the article content element (for embedding in other documents)
pub fn extract_article(file_path) element^ {
    let result = parse(file_path)?
    result.content
}

/// Get clean article HTML string from file
pub fn to_html(file_path) string^ {
    let result = parse(file_path)?
    if (result.content != null) format(result.content, 'html')
    else ""
}

/// Quick check if document is probably readable (isProbablyReaderable equivalent)
pub fn is_readable(file_path, min_content_length, min_score) bool^ {
    let min_len = if (min_content_length != null) min_content_length else MIN_CONTENT_LENGTH
    let min_sc = if (min_score != null) min_score else MIN_SCORE
    let doc = input(file_path, 'html')?
    is_doc_readable(doc, min_len, min_sc)
}

/// Check if parsed document is readable
pub fn is_doc_readable(doc, min_content_length, min_score) => (
    let min_len = if (min_content_length != null) min_content_length else MIN_CONTENT_LENGTH,
    let min_sc = if (min_score != null) min_score else MIN_SCORE,
    
    let body = find_deep(doc, "body"),
    if (body == null) false
    else (
        let ps = find_all(body, "p"),
        let pres = find_all(body, "pre"),
        let articles = find_all(body, "article"),
        let nodes = ps ++ pres ++ articles,
        
        // filter to visible nodes with enough content
        // Score by text length minus minimum (simpler than original sqrt-based)
        let good_nodes = [for (node in nodes,
                          let is_visible = is_probably_visible(node),
                          let is_likely = not is_unlikely_candidate(node),
                          let text_len = len(trim(get_text(node)))
                          where is_visible and is_likely and text_len >= min_len) 
                         text_len - min_len],
        
        let total_score = sum(good_nodes),
        total_score > min_sc
    )
)

/// Helper to ensure doc is parsed from file path or already an element
/// Returns element on success, propagates error from input()
fn ensure_parsed(doc) element^ =>
    if (doc is element) doc
    else input(doc, 'html')?

/// Extract only the title
pub fn title(doc) string^ {
    let parsed = ensure_parsed(doc)?
    extract_title(parsed)
}

/// Extract only the text content
pub fn text(doc) string^ {
    let parsed = ensure_parsed(doc)?
    let body = find_deep(parsed, "body")
    if (body == null) "" else get_inner_text(body, true)
}

/// Get the language
pub fn lang(doc) string^ {
    let parsed = ensure_parsed(doc)?
    get_lang(parsed)
}

/// Get text direction
pub fn dir(doc) string^ {
    let parsed = ensure_parsed(doc)?
    get_dir(parsed)
}

/// Get metadata only
pub fn metadata(doc) map^ {
    let parsed = ensure_parsed(doc)?
    extract_metadata(parsed, null)
}
