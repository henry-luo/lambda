// Readability - Extract main content from HTML documents
// Port of Mozilla Readability.js to Lambda Script
//
// Usage:
//   import readability: .utils.readability
//
//   // Parse HTML file and extract readable content
//   let result = readability.parse("article.html")
//
//   // Result contains:
//   //   title: string?        - Article title
//   //   byline: string?       - Author info
//   //   content: element?     - Extracted content element
//   //   textContent: string   - Clean text without HTML
//   //   length: int           - Text length
//   //   excerpt: string?      - Article excerpt/summary
//   //   siteName: string?     - Site name
//   //   lang: string?         - Language code
//   //   dir: string?          - Text direction (ltr/rtl)
//   //   publishedTime: string? - Publication timestamp
//
// Based on Mozilla Readability.js (https://github.com/mozilla/readability)

// ============================================
// Configuration Constants
// ============================================

let DEFAULT_CHAR_THRESHOLD = 500
let DEFAULT_N_TOP_CANDIDATES = 5
let MIN_CONTENT_LENGTH = 140
let MIN_SCORE = 20

// Flags for retry logic
let FLAG_STRIP_UNLIKELYS = 1
let FLAG_WEIGHT_CLASSES = 2
let FLAG_CLEAN_CONDITIONALLY = 4

// Pattern lists for candidate classification
let UNLIKELY_CANDIDATES = [
    "-ad-", "ai2html", "banner", "breadcrumbs", "combx", "comment", "community",
    "cover-wrap", "disqus", "extra", "footer", "gdpr", "header", "legends",
    "menu", "related", "remark", "replies", "rss", "shoutbox", "sidebar",
    "skyscraper", "social", "sponsor", "supplemental", "ad-break", "agegate",
    "pagination", "pager", "popup", "yom-remote"
]

let OK_MAYBE_CANDIDATE = ["and", "article", "body", "column", "content", "main", "shadow"]

let POSITIVE_PATTERNS = [
    "article", "body", "content", "entry", "hentry", "h-entry", "main",
    "page", "pagination", "post", "text", "blog", "story"
]

let NEGATIVE_PATTERNS = [
    "-ad-", "hidden", "hid", "banner", "combx", "comment", "com-", "contact",
    "footer", "gdpr", "masthead", "media", "meta", "outbrain", "promo",
    "related", "scroll", "share", "shoutbox", "sidebar", "skyscraper",
    "sponsor", "shopping", "tags", "widget"
]

let BYLINE_PATTERNS = ["byline", "author", "dateline", "writtenby", "p-author"]

let UNLIKELY_ROLES = ["menu", "menubar", "complementary", "navigation", "alert", "alertdialog", "dialog"]

// Tags that should be scored (as symbols for fast comparison)
let TAGS_TO_SCORE = ['section', 'h2', 'h3', 'h4', 'h5', 'h6', 'p', 'td', 'pre']

// Block-level elements
let DIV_TO_P_ELEMS = ['blockquote', 'dl', 'div', 'img', 'ol', 'p', 'pre', 'table', 'ul']

// Tags to alter to div, except these
let ALTER_TO_DIV_EXCEPTIONS = ['div', 'article', 'section', 'p', 'ol', 'ul']

// Video site patterns
let VIDEO_SITES = ["youtube", "vimeo", "dailymotion", "twitch", "bilibili"]

// Ad words in various languages
let AD_WORDS = ["ad", "ads", "advert", "advertisement", "pub", "publicite", "anzeige", "werbung"]

// Share element patterns
let SHARE_PATTERNS = ["share", "sharedaddy"]

// Tags that should be removed entirely
let REMOVE_TAGS = ['script', 'style', 'noscript', 'link']

// Presentational attributes to strip
let PRESENTATIONAL_ATTRS = ["align", "background", "bgcolor", "border", "cellpadding",
    "cellspacing", "frame", "hspace", "rules", "style", "valign", "vspace", "width", "height"]

// Deprecated size attributes
let DEPRECATED_SIZE_ATTR_ELEMS = ['table', 'th', 'td', 'hr', 'pre']

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

// Count commas in text (including unicode comma variants)
fn count_commas(str) =>
    if (str == null) 0
    else (
        let parts = split(str, ","),
        let count1 = len(parts) - 1,
        // Also count Chinese/Japanese commas
        let parts2 = split(str, "\u3001"),
        let count2 = len(parts2) - 1,
        let parts3 = split(str, "\uFF0C"),
        let count3 = len(parts3) - 1,
        count1 + count2 + count3
    )

// Text similarity - token-based comparison (port of Readability._textSimilarity)
fn text_similarity(text_a, text_b) =>
    if (text_a == null or text_b == null) 0.0
    else (
        let tokens_a = split(lower(text_a), " "),
        let tokens_b = split(lower(text_b), " "),
        let a_set = [for (t in tokens_a where len(trim(t)) > 0) trim(t)],
        let b_set = [for (t in tokens_b where len(trim(t)) > 0) trim(t)],
        if (len(b_set) == 0) 0.0
        else (
            // Tokens in B not in A
            let uniq_b = [for (t in b_set where not (t in a_set)) t],
            let dist_b = len(str_join(uniq_b, " ")) * 1.0 / (len(str_join(b_set, " ")) * 1.0),
            1.0 - dist_b
        )
    )

// Unescape basic HTML entities
fn unescape_html(str) =>
    if (str == null) ""
    else (
        let s1 = replace(str, "&amp;", "&"),
        let s2 = replace(s1, "&lt;", "<"),
        let s3 = replace(s2, "&gt;", ">"),
        let s4 = replace(s3, "&quot;", "\""),
        replace(s4, "&#39;", "'")
    )

// ============================================
// Element Helper Functions
// ============================================

// Get tag name as symbol (using name() for HTML-parsed elements)
fn tag_of(elem) => elem_name(elem)

// Check if element has specific tag
fn has_tag(elem, tag_name) =>
    tag_of(elem) == symbol(lower(tag_name))

// Get attribute value or default
fn get_attr(elem, attr_name, default_val) =>
    if (not (elem is element)) default_val
    else (
        let val = elem[symbol(attr_name)],
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

// Check if element has any of the given tags (as symbol list)
fn has_any_tag(elem, tag_list) =>
    if (not (elem is element)) false
    else (
        let elem_tag = tag_of(elem),
        elem_tag in tag_list
    )

// Get element's role attribute
fn get_role(elem) =>
    get_attr(elem, "role", "")

// Check if node is an element
fn is_elem(node) => node is element

// Check if an element has any child that is a block-level element
// Used for DIV→P conversion logic
fn has_child_block_element(elem) =>
    if (not (elem is element)) false
    else (
        let n = len(elem),
        if (n == 0) false
        else (
            let block_children = [for (i in 0 to n - 1,
                                       let child = elem[i]
                                       where is_elem(child) and tag_of(child) in DIV_TO_P_ELEMS)
                                  child],
            len(block_children) > 0
        )
    )

// Check if a div should be treated as a paragraph (no block children)
fn is_div_as_paragraph(elem) =>
    if (not (elem is element)) false
    else tag_of(elem) == 'div' and not has_child_block_element(elem)

// ============================================
// Text Content Extraction
// ============================================

// Get text content from element (recursive, skips hidden elements)
fn get_text(elem) =>
    if (elem is string) elem
    else if (elem == null) ""
    else if (not (elem is element)) string(elem)
    else (
        // Skip script, style, noscript elements (like Readability.js _removeScripts)
        let tag = tag_of(elem),
        if (tag == 'script' or tag == 'style' or tag == 'noscript') ""
        else (
            // Skip aria-hidden="true" and hidden elements
            let aria_hidden = get_attr(elem, "aria-hidden", ""),
            let hidden_attr = get_attr(elem, "hidden", null),
            let style = get_attr(elem, "style", ""),
            let is_hidden = aria_hidden == "true" or
                            hidden_attr != null or
                            contains(lower(style), "display:none") or
                            contains(lower(style), "display: none") or
                            contains(lower(style), "visibility:hidden") or
                            contains(lower(style), "visibility: hidden"),
            if (is_hidden) ""
            else (
                let n = len(elem),
                if (n == 0) ""
                else (
                    let parts = [for (i in 0 to n - 1) get_text(elem[i])],
                    str_join(parts, "")
                )
            )
        )
    )

// Get inner text with normalized whitespace
fn get_inner_text(elem, do_normalize) => (
    let text = get_text(elem),
    if (do_normalize) normalize_whitespace(text) else trim(text)
)

// Get text density for specific child tags
fn get_text_density(elem, tag_symbols) => (
    let total_text = len(get_inner_text(elem, true)),
    if (total_text == 0) 0.0
    else (
        let matching_elems = collect_by_tags(elem, tag_symbols),
        let matching_text = sum([for (e in matching_elems) len(get_inner_text(e, true))]),
        (matching_text * 1.0) / (total_text * 1.0)
    )
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
                                where is_elem(child) and tag_of(child) == target) child],
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
            let found = [for (r in results where is_elem(r)) r],
            if (len(found) > 0) found[0] else null
        )
    )

// Helper function for find_all - recursive collect
fn find_all_helper(e, target) =>
    if (e == null or not (e is element)) []
    else (
        let matches = if (tag_of(e) == target) [e] else [],
        let n = len(e),
        let child_matches = [for (i in 0 to n - 1) for (m in find_all_helper(e[i], target)) m],
        matches ++ child_matches
    )

// Collect all matching elements
fn find_all(elem, tag_name) => (
    let target = symbol(lower(tag_name)),
    find_all_helper(elem, target)
)

// Collect elements by multiple tag symbols
fn collect_by_tags(elem, tag_symbols) =>
    if (elem == null or not (elem is element)) []
    else (
        let self_match = if (tag_of(elem) in tag_symbols) [elem] else [],
        let n = len(elem),
        let child_matches = [for (i in 0 to n - 1) for (m in collect_by_tags(elem[i], tag_symbols)) m],
        self_match ++ child_matches
    )

// Collect all descendant elements (recursive, depth-first/document order)
// Skips hidden subtrees (visibility:hidden, display:none, aria-hidden, hidden attr)
fn collect_all_elements(elem) =>
    if (not (elem is element)) []
    else if (not is_probably_visible(elem)) []
    else (
        let n = len(elem),
        [for (i in 0 to n - 1,
              let child = elem[i]
              where is_elem(child))
         for (e in [child] ++ collect_all_elements(child))
         e]
    )

// Collect elements, skipping subtrees of unlikely candidates
// This mimics Readability.js _grabArticle behavior where unlikely containers
// (e.g. elements with "header", "footer", "sidebar" in class/id) are removed
// before byline search, preventing bylines inside unlikely containers from being found
fn collect_elements_skip_unlikely(elem) =>
    if (not (elem is element)) []
    else if (not is_probably_visible(elem)) []
    else if (is_unlikely_candidate(elem)) []
    else (
        let n = len(elem),
        [for (i in 0 to n - 1,
              let child = elem[i]
              where is_elem(child))
         for (e in [child] ++ collect_elements_skip_unlikely(child))
         e]
    )

// has_child_block_element is defined above (line ~215)

// Check if element is without content (empty or only br/hr)
fn is_element_without_content(elem) =>
    if (not (elem is element)) false
    else (
        let text = trim(get_text(elem)),
        len(text) == 0 and (
            len(elem) == 0 or
            (
                let non_trivial = [for (i in 0 to len(elem) - 1,
                                       let child = elem[i]
                                       where is_elem(child) and
                                             not (tag_of(child) == 'br' or tag_of(child) == 'hr'))
                                   true],
                len(non_trivial) == 0
            )
        )
    )

// ============================================
// Scoring Functions
// ============================================

// Get class/id weight
fn get_class_weight(elem, flags) => (
    if ((flags div FLAG_WEIGHT_CLASSES) % 2 == 0) 0
    else (
        let class_str = lower(get_class(elem)),
        let id_str = lower(get_id(elem)),
        let w0 = 0,
        // Check class
        let w1 = if (matches_any(class_str, NEGATIVE_PATTERNS)) w0 - 25 else w0,
        let w2 = if (matches_any(class_str, POSITIVE_PATTERNS)) w1 + 25 else w1,
        // Check id
        let w3 = if (matches_any(id_str, NEGATIVE_PATTERNS)) w2 - 25 else w2,
        let w4 = if (matches_any(id_str, POSITIVE_PATTERNS)) w3 + 25 else w3,
        w4
    )
)

// Calculate link density (ratio of link text to total text)
fn get_link_density(elem) => (
    let text_length = len(get_inner_text(elem, true)),
    if (text_length == 0) 0.0
    else (
        let links = find_all(elem, "a"),
        let link_lengths = [for (link in links) (
            let href = get_attr(link, "href", ""),
            let coeff = if (starts_with(href, "#")) 0.3 else 1.0,
            len(get_inner_text(link, true)) * coeff
        )],
        let link_length = sum(link_lengths),
        link_length / (text_length * 1.0)
    )
)

// Initialize score for an element based on tag
fn get_initial_score(elem) => (
    let tag = tag_of(elem),
    if (tag == 'div') 5
    else if (tag == 'pre' or tag == 'td' or tag == 'blockquote') 3
    else if (tag == 'address' or tag == 'ol' or tag == 'ul' or
             tag == 'dl' or tag == 'dd' or tag == 'dt' or
             tag == 'li' or tag == 'form') -3
    else if (tag == 'h1' or tag == 'h2' or tag == 'h3' or
             tag == 'h4' or tag == 'h5' or tag == 'h6' or tag == 'th') -5
    else 0
)

// Check if element is probably visible
fn is_probably_visible(elem) =>
    if (not (elem is element)) false
    else (
        let style = lower(get_attr(elem, "style", "")),
        let hidden = get_attr(elem, "hidden", null),
        let aria_hidden = get_attr(elem, "aria-hidden", ""),
        let class_str = get_class(elem),

        let style_ok = not (contains(style, "display:none") or
             contains(style, "display: none") or
             contains(style, "visibility:hidden") or
             contains(style, "visibility: hidden")),
        let hidden_ok = if (hidden == null) true else false,
        let aria_ok = if (aria_hidden != "true") true
                      else contains(class_str, "fallback-image"),
        style_ok and hidden_ok and aria_ok
    )

// Check if unlikely candidate
fn is_unlikely_candidate(elem) => (
    let match_string = lower(get_match_string(elem)),
    let is_unlikely = matches_any(match_string, UNLIKELY_CANDIDATES),
    let is_maybe_ok = matches_any(match_string, OK_MAYBE_CANDIDATE),
    let role = get_role(elem),
    let has_unlikely_role = role in UNLIKELY_ROLES,
    let tag = tag_of(elem),
    // Don't strip body or a tags
    let is_protected = tag == 'body' or tag == 'a',
    not is_protected and ((is_unlikely and not is_maybe_ok) or has_unlikely_role)
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
// Metadata Extraction
// ============================================

// Extract title from document - port of _getArticleTitle
fn extract_title(doc) => (
    let head = find_deep(doc, "head"),
    let title_elem0 = if (head != null) find_deep(head, "title") else null,
    // Fallback: search entire doc tree (parser may place title under body)
    let title_elem = if (title_elem0 != null) title_elem0 else find_deep(doc, "title"),

    let orig_title = if (title_elem != null) normalize_whitespace(get_text(title_elem)) else "",
    let cur_title = orig_title,

    // Title separator characters: | - – — \ / > »
    let separators = [" | ", " - ", " \u2013 ", " \u2014 ", " \\ ", " / ", " > ", " \u00BB "],
    // Hierarchical separators: \ / > »  (NOT | or -)
    let hier_separators = [" \\ ", " / ", " > ", " \u00BB "],

    let has_sep = len([for (s in separators where contains(cur_title, s)) true]) > 0,
    let has_hier_sep = len([for (s in hier_separators where contains(cur_title, s)) true]) > 0,
    let has_colon = contains(cur_title, ": "),

    let title_from_sep = if (has_sep) (
        // Find the LAST separator and take text before it
        // (mimics Readability.js greedy regex behavior)
        let last_sep_idx = max([for (s in separators,
                                     let idx = last_index_of(cur_title, s)
                                     where idx >= 0)
                                idx]),
        let before_last = trim(slice(cur_title, 0, last_sep_idx)),

        // If resulting title < 3 words, try removing text before FIRST separator
        if (word_count(before_last) < 3) (
            let first_sep_idx = min([for (s in separators,
                                         let idx = index_of(cur_title, s)
                                         where idx >= 0)
                                    idx]),
            let sep_len = len([for (s in separators where index_of(cur_title, s) == first_sep_idx) s][0]),
            trim(slice(cur_title, first_sep_idx + sep_len, len(cur_title)))
        )
        else before_last
    )
    else if (has_colon) (
        // Check if any h1/h2 contains exact title text
        let headings = find_all(doc, "h1") ++ find_all(doc, "h2"),
        let exact_match = len([for (h in headings
                                   where trim(get_inner_text(h, true)) == trim(cur_title))
                              true]) > 0,
        if (exact_match) cur_title
        else (
            // Use text after last colon
            let last_colon = last_index_of(cur_title, ": "),
            let after_last = trim(slice(cur_title, last_colon + 2, len(cur_title))),

            if (word_count(after_last) < 3) (
                // Try first colon
                let first_colon = index_of(cur_title, ": "),
                let after_first = trim(slice(cur_title, first_colon + 2, len(cur_title))),
                // But if too many words before colon, use original
                if (word_count(slice(cur_title, 0, first_colon)) > 5) cur_title
                else after_first
            )
            else if (word_count(slice(cur_title, 0, index_of(cur_title, ": "))) > 5) cur_title
            else after_last
        )
    )
    else if (len(cur_title) > 150 or len(cur_title) < 15) (
        let h1s = find_all(doc, "h1"),
        if (len(h1s) == 1) get_inner_text(h1s[0], true)
        else cur_title
    )
    else cur_title,

    let trimmed = normalize_whitespace(trim(title_from_sep)),

    // If title has 4 words or fewer AND (no hierarchical separators OR
    // decreased word count by more than 1), use original title
    let cur_wc = word_count(trimmed),
    // Count words in original without separators (replace all seps with space)
    let s1 = replace(orig_title, " | ", " "),
    let s2 = replace(s1, " - ", " "),
    let s3 = replace(s2, " \u2013 ", " "),
    let s4 = replace(s3, " \u2014 ", " "),
    let s5 = replace(s4, " \\ ", " "),
    let s6 = replace(s5, " / ", " "),
    let s7 = replace(s6, " > ", " "),
    let s8 = replace(s7, " \u00BB ", " "),
    let orig_no_sep_wc = word_count(s8),

    let final_title = if (cur_wc <= 4 and
                         (not has_hier_sep or cur_wc != orig_no_sep_wc - 1))
                         orig_title
                     else trimmed,

    normalize_whitespace(final_title)
)

// Helper for checking name or property match - handles null safely
// Normalizes names: dots→colons and lowercased (DC.Title → dc:title)
fn meta_key_matches(meta_name, meta_property, query_key) =>
    if (query_key == "" or query_key == null) false
    else (
        let lk = lower(query_key),
        // Handle space-separated property values (e.g., property="dc:title og:title")
        // Normalize dots to colons for DC.* meta names (DC.Title → dc:title)
        let prop_parts = if (meta_property != "" and meta_property != null)
                             split(trim(meta_property), " ") else [],
        let prop_match = len([for (p in prop_parts
                                  where lower(replace(p, ".", ":")) == lk) p]) > 0,
        if (prop_match) true
        else (
            // For name attribute, use Readability.js name pattern rules:
            // Valid prefixes: dc, dcterm, og, twitter, parsely, weibo:article, weibo:webpage
            // Note: "article" is NOT a valid name prefix (only valid as property prefix)
            let name_parts = if (meta_name != "" and meta_name != null)
                                 split(trim(meta_name), " ") else [],
            len([for (p in name_parts
                      where lower(replace(p, ".", ":")) == lk) p]) > 0
        )
    )

// Get meta content by key - returns LAST matching meta (Readability.js overwrites duplicates)
fn get_meta(doc, key) => (
    let metas = find_all(doc, "meta"),
    let matched = [for (meta in metas,
                        let meta_name = get_attr(meta, "name", ""),
                        let meta_property = get_attr(meta, "property", ""),
                        let content = get_attr(meta, "content", ""),
                        let has_content = content != "" and content != null
                        where has_content and meta_key_matches(meta_name, meta_property, key))
                   content],
    if (len(matched) > 0) matched[len(matched) - 1] else null
)

// Get first non-null meta value from a list of keys (priority-ordered)
fn get_first_meta(doc, keys) => (
    let results = [for (k in keys,
                        let val = get_meta(doc, k)
                        where val != null)
                   val],
    if (len(results) > 0) results[0] else null
)

// Extract JSON-LD metadata from <script type="application/ld+json"> tags
fn get_json_ld(doc) => (
    // Search in both head and body for JSON-LD scripts
    let head = find_deep(doc, "head"),
    let body = find_deep(doc, "body"),
    let head_scripts = if (head != null) find_all(head, "script") else [],
    let body_scripts = if (body != null) find_all(body, "script") else [],
    let all_scripts = head_scripts ++ body_scripts,
    let json_ld_scripts = [for (s in all_scripts
                                where get_attr(s, "type", "") == "application/ld+json")
                           s],
    if (len(json_ld_scripts) == 0) null
    else try_parse_json_ld(json_ld_scripts, 0)
)

// ============================================
// JSON-LD String-Based Extraction
// ============================================
// Lambda's input() only works with file paths, so we use string operations
// to extract fields from JSON-LD text.

// Find the end of a JSON string, handling escaped quotes
fn find_end_quote(text, pos) =>
    if (pos >= len(text)) null
    else if (slice(text, pos, pos + 1) == "\\") find_end_quote(text, pos + 2)
    else if (slice(text, pos, pos + 1) == "\"") slice(text, 0, pos)
    else find_end_quote(text, pos + 1)

// Extract a string value for a JSON key: "key": "value"
fn json_get_string(text, key) => (
    let needle = "\"" ++ key ++ "\"",
    let pos = index_of(text, needle),
    if (pos < 0) null
    else (
        let after = slice(text, pos + len(needle), len(text)),
        let trimmed = trim(after),
        if (len(trimmed) == 0 or slice(trimmed, 0, 1) != ":") null
        else (
            let val_part = trim(slice(trimmed, 1, len(trimmed))),
            if (len(val_part) == 0) null
            else if (slice(val_part, 0, 1) == "\"")
                find_end_quote(slice(val_part, 1, len(val_part)), 0)
            else null
        )
    )
)

// Get value from nested object: "outerKey": {"innerKey": "value"}
// Returns the string value, or if outerKey is a plain string, returns that
fn json_get_nested_string(text, outer_key, inner_key) => (
    let needle = "\"" ++ outer_key ++ "\"",
    let pos = index_of(text, needle),
    if (pos < 0) null
    else (
        let after = slice(text, pos + len(needle), len(text)),
        let trimmed = trim(after),
        if (len(trimmed) == 0 or slice(trimmed, 0, 1) != ":") null
        else (
            let val_part = trim(slice(trimmed, 1, len(trimmed))),
            if (len(val_part) == 0) null
            else if (slice(val_part, 0, 1) == "{")
                json_get_string(val_part, inner_key)
            else if (slice(val_part, 0, 1) == "\"")
                find_end_quote(slice(val_part, 1, len(val_part)), 0)
            else null
        )
    )
)

// Get author from JSON-LD text - only handles object and array forms
// Readability.js does NOT extract plain string authors ("author": "name")
// Only: {"author": {"name": "..."}} or {"author": [{"name": "..."}]}
fn json_get_author(text) => (
    let needle = "\"author\"",
    let pos = index_of(text, needle),
    if (pos < 0) null
    else (
        let after = slice(text, pos + len(needle), len(text)),
        let trimmed = trim(after),
        if (len(trimmed) == 0 or slice(trimmed, 0, 1) != ":") null
        else (
            let val_part = trim(slice(trimmed, 1, len(trimmed))),
            if (len(val_part) == 0) null
            // Plain string author — skip (Readability.js doesn't handle this)
            else if (slice(val_part, 0, 1) == "\"") null
            else if (slice(val_part, 0, 1) == "{")
                json_get_string(val_part, "name")
            else if (slice(val_part, 0, 1) == "[")
                json_get_string(val_part, "name")
            else null
        )
    )
)

// Try parsing JSON-LD scripts sequentially, skip non-article types
// Script children can be either Lambda maps (parsed by HTML parser) or strings (CDATA-wrapped)
fn try_parse_json_ld(scripts, idx) =>
    if (idx >= len(scripts)) null
    else (
        let s = scripts[idx],
        let child0 = if (len(s) > 0) s[0] else null,
        let result = if (child0 == null) null
                     else if (child0 is string) (
                         // String child — HTML parser didn't parse JSON (e.g. CDATA-wrapped)
                         let raw_text = trim(child0),
                         let text = if (starts_with(raw_text, "<![CDATA["))
                                        trim(slice(raw_text, 9, len(raw_text) - 3))
                                    else raw_text,
                         if (len(text) == 0) null
                         else extract_json_ld_from_text(text)
                     )
                     // map child — HTML parser already parsed JSON into Lambda map
                     else (
                         extract_json_ld_from_map(child0)
                     ),
        if (result != null) result
        else try_parse_json_ld(scripts, idx + 1)
    )

// Extract metadata from a JSON-LD Lambda map (parsed by HTML parser)
fn extract_json_ld_from_map(data) => (
    let context = data["@context"],
    let has_schema = if (context != null) contains(lower(string(context)), "schema.org")
                     else false,
    if (not has_schema) null
    else (
        let type_str = data["@type"],
        if (not is_article_type(type_str)) null
        else (
            let headline = data["headline"],
            let name_val = data[symbol("name")],
            let jld_title = if (headline != null) string(headline)
                            else if (name_val != null) string(name_val)
                            else null,
            let author_raw = data["author"],
            // Readability.js only extracts author as:
            // 1. object with .name string: {name: "John"} → "John"
            // 2. array of objects with .name: [{name: "John"}, {name: "Jane"}] → "John, Jane"
            // It does NOT extract plain string authors (e.g., author: "mcupdate")
            let author_str = if (author_raw == null) null
                             else if (author_raw is string) null
                             else if (is_elem(author_raw)) null
                             else if (author_raw is list) (
                                 let first_name = author_raw[0],
                                 if (first_name != null and not (first_name is string)) (
                                     let fn = first_name[symbol("name")],
                                     if (fn != null) string(fn) else null
                                 )
                                 else null
                             )
                             else (
                                 let aname = author_raw[symbol("name")],
                                 if (aname != null) string(aname) else null
                             ),
            let excerpt_raw = data["description"],
            let excerpt = if (excerpt_raw != null) string(excerpt_raw) else null,
            let pub_raw = data["publisher"],
            let site_name = if (pub_raw == null) null
                            else if (pub_raw is string) pub_raw
                            else (
                                let pname = pub_raw[symbol("name")],
                                if (pname != null) string(pname) else null
                            ),
            let pub_time_raw = data["datePublished"],
            let pub_time = if (pub_time_raw != null) string(pub_time_raw) else null,

            {
                title: if (jld_title != null) unescape_html(jld_title) else null,
                byline: if (author_str != null) unescape_html(author_str) else null,
                excerpt: if (excerpt != null) unescape_html(excerpt) else null,
                siteName: if (site_name != null) unescape_html(site_name) else null,
                publishedTime: pub_time
            }
        )
    )
)

// Extract metadata from JSON-LD text using string extraction
fn extract_json_ld_from_text(text) => (
    // Check for schema.org context
    let context = json_get_string(text, "@context"),
    let has_schema = if (context != null) contains(lower(context), "schema.org")
                     else false,
    if (not has_schema) null
    else (
        let type_str = json_get_string(text, "@type"),
        if (not is_article_type(type_str)) null
        else (
            let headline = json_get_string(text, "headline"),
            let name_val = json_get_string(text, "name"),
            let jld_title = if (headline != null) headline
                            else if (name_val != null) name_val
                            else null,
            let author_str = json_get_author(text),
            let excerpt = json_get_string(text, "description"),
            let site_name = json_get_nested_string(text, "publisher", "name"),
            let pub_time = json_get_string(text, "datePublished"),

            {
                title: if (jld_title != null) unescape_html(jld_title) else null,
                byline: if (author_str != null) unescape_html(author_str) else null,
                excerpt: if (excerpt != null) unescape_html(excerpt) else null,
                siteName: if (site_name != null) unescape_html(site_name) else null,
                publishedTime: pub_time
            }
        )
    )
)

// Check if JSON-LD type matches article types
fn is_article_type(type_str) =>
    if (type_str == null) false
    else (
        let t = string(type_str),
        t == "Article" or t == "NewsArticle" or t == "BlogPosting" or
        t == "Report" or t == "ScholarlyArticle" or t == "MedicalScholarlyArticle" or
        t == "SocialMediaPosting" or t == "LiveBlogPosting" or
        t == "TechArticle" or t == "APIReference" or t == "OpinionNewsArticle" or
        t == "ReportageNewsArticle" or t == "AnalysisNewsArticle" or
        t == "AskPublicNewsArticle" or t == "BackgroundNewsArticle" or
        t == "ReviewNewsArticle" or starts_with(t, "http://schema.org/") or
        starts_with(t, "https://schema.org/")
    )

// Extract metadata from parsed JSON-LD
// Extract article metadata - port of _getArticleMetadata
fn extract_metadata(doc, json_ld_param) => (
    let jld0 = if (json_ld_param != null) json_ld_param else get_json_ld(doc),
    let jld = if (jld0 != null) jld0 else {},

    // Title priority: json-ld → dc:title → dcterm:title → og:title → weibo:article:title →
    //                 weibo:webpage:title → title → twitter:title → parsely-title → extract_title
    let meta_title = get_first_meta(doc, ["dc:title", "dcterm:title", "og:title",
                                          "weibo:article:title", "weibo:webpage:title",
                                          "title", "twitter:title", "parsely-title"]),
    let title = if (jld.title != null) jld.title
                else if (meta_title != null) meta_title
                else extract_title(doc),

    // Byline priority: json-ld → dc:creator → dcterm:creator → author → parsely-author
    // Note: Readability.js also uses article:author as lowest priority (if not a URL)
    // but we skip it since meta name="article:author" rarely matters
    let meta_byline = get_first_meta(doc, ["dc:creator", "dcterm:creator", "author", "parsely-author"]),
    let byline = if (jld.byline != null) jld.byline
                 else if (meta_byline != null and len(trim(meta_byline)) > 0) meta_byline
                 else null,

    // Excerpt priority: json-ld → dc:description → dcterm:description → og:description →
    //                   weibo:article:description → weibo:webpage:description → description → twitter:description
    let meta_excerpt = get_first_meta(doc, ["dc:description", "dcterm:description", "og:description",
                                            "weibo:article:description", "weibo:webpage:description",
                                            "description", "twitter:description"]),
    let excerpt = if (jld.excerpt != null) jld.excerpt
                  else meta_excerpt,

    let siteName = if (jld.siteName != null) jld.siteName
                   else get_meta(doc, "og:site_name"),

    let publishedTime = if (jld.publishedTime != null) jld.publishedTime
                        else get_first_meta(doc, ["article:published_time", "parsely-pub-date"]),

    {
        title: if (title != null) unescape_html(title) else null,
        byline: if (byline != null) unescape_html(byline) else null,
        excerpt: if (excerpt != null) unescape_html(excerpt) else null,
        siteName: if (siteName != null) unescape_html(siteName) else null,
        publishedTime: publishedTime
    }
)

// Get document language
fn get_lang(doc) => (
    let html = find_deep(doc, "html"),
    if (html != null) (
        let lang = get_attr(html, "lang", null),
        if (lang != null) lang
        else get_attr(html, "xml:lang", null)
    )
    else null
)

// Get text direction
fn get_dir(doc) => (
    let html = find_deep(doc, "html"),
    let body = find_deep(doc, "body"),
    let main_elem = if (body != null) find_child(body, "main") else null,
    let html_dir = if (html != null) get_attr(html, "dir", null) else null,
    let body_dir = if (body != null) get_attr(body, "dir", null) else null,
    let main_dir = if (main_elem != null) get_attr(main_elem, "dir", null) else null,
    // Return the deepest (most specific) ancestor dir found
    // Note: don't check the article element itself, only its ancestors
    if (main_dir != null) main_dir
    else if (body_dir != null) body_dir
    else html_dir
)

// ============================================
// Content Scoring & Selection (Core Algorithm)
// ============================================

// Get ancestors of an element up to maxDepth
// Returns list of {elem, depth} where depth 0 is parent
fn get_ancestors(all_elems, elem_idx, max_depth) =>
    // Walk up the tree to find ancestors
    // Since we don't have parent pointers, we use the flat list to find containers
    []

// Score paragraph-level elements and propagate to ancestors
// This is the core of Readability's algorithm
// Port of _grabArticle scoring loop
//
// Since Lambda doesn't have parent pointers or mutable maps, we use a different
// approach: score each candidate container directly by examining its children.
// NOTE: We store indices instead of element references in scored maps to avoid
// Lambda bug where element references in maps can be lost.

// Check if an element is a scorable paragraph-like element
fn is_scorable_para(e) => (
    let tag = tag_of(e),
    tag == 'p' or tag == 'pre' or tag == 'td' or is_div_as_paragraph(e)
)

// Calculate content score for a single paragraph element
fn calc_para_score(p) => (
    let text = get_inner_text(p, true),
    let tlen = len(text),
    if (tlen < 25) 0
    else 1 + count_commas(text) + min(floor(tlen / 100), 3)
)

// Get direct element children of a container
fn direct_elem_children(c) => (
    let n = len(c),
    [for (i in 0 to n - 1,
          let ch = c[i]
          where is_elem(ch))
     ch]
)

fn score_candidates(body, flags) => (
    // Collect containers using find_all for essential tag types.
    // IMPORTANT: find_all preserves element children, unlike collect_all_elements.
    // Reduced to core tag types for performance (div, section, article, main cover 99% of cases).
    let strip_unlikely = (flags div FLAG_STRIP_UNLIKELYS) % 2 == 1,
    let tag_containers = [for (tag in ['div', 'section', 'article', 'main'])
                           for (e in find_all(body, tag)) e],
    let all_containers = tag_containers ++ [body],

    // Filter to visible, likely candidates
    let containers = [for (e in all_containers
                          where is_probably_visible(e) and not (
                              strip_unlikely and is_unlikely_candidate(e)
                          ))
                     e],

    // Score each container using proximity-weighted paragraph scoring.
    // This matches Readability.js behavior where paragraphs propagate their score
    // to parent (full), grandparent (÷2), great-grandparent (÷6), etc.
    // Results are stored as [score, text_len, container_element] arrays to avoid
    // storing element references in maps (Lambda limitation).
    let scored = [for (c in containers,
                       let inner_text = get_inner_text(c, true),
                       let text_len = len(inner_text)
                       where text_len >= 25)
                  (
                      let kids = direct_elem_children(c),

                      // Level 0: direct scorable paragraph children → full score (÷1)
                      let l0_paras = [for (ch in kids where is_scorable_para(ch)) ch],
                      let l0_score = sum([for (p in l0_paras) calc_para_score(p)]),

                      // Non-paragraph direct children (containers for deeper levels)
                      let non_para_kids = [for (ch in kids where not is_scorable_para(ch)) ch],

                      // Level 1: scorable grandchildren → half score (÷2)
                      let l1_kids = [for (ch in non_para_kids)
                                      for (g in direct_elem_children(ch)) g],
                      let l1_paras = [for (p in l1_kids where is_scorable_para(p)) p],
                      let l1_score = sum([for (p in l1_paras) calc_para_score(p)]) / 2.0,

                      // Level 2: great-grandchildren → 1/6 score (÷6)
                      let l1_non_para = [for (p in l1_kids where not is_scorable_para(p)) p],
                      let l2_kids = [for (ch in l1_non_para)
                                      for (g in direct_elem_children(ch)) g],
                      let l2_paras = [for (p in l2_kids where is_scorable_para(p)) p],
                      let l2_score = sum([for (p in l2_paras) calc_para_score(p)]) / 6.0,

                      let content_score = l0_score + l1_score + l2_score,

                      // Add initial score and class weight
                      let init_score = get_initial_score(c),
                      let class_wt = get_class_weight(c, flags),
                      let raw_score = init_score + class_wt + content_score,

                      // Apply link density penalty
                      let ld = get_link_density(c),
                      let final_score = raw_score * (1.0 - ld),

                      // Return [score, text_len, element] array
                      [final_score, text_len, c]
                  )],

    // Filter to meaningful scores (score at index 0)
    [for (s in scored where s[0] > 0) s]
)

// Find the best content candidate - port of _grabArticle's candidate selection
fn find_best_candidate(doc, flags) => (
    let body = find_deep(doc, "body"),
    if (body == null) null
    else (
        // First, check for semantic elements
        let articles = find_all(body, "article"),
        let visible_articles = [for (a in articles
                                    where is_probably_visible(a) and has_content(a) and
                                          len(get_inner_text(a, true)) >= DEFAULT_CHAR_THRESHOLD)
                                a],
        // Also check with lower threshold for single articles with some content
        let any_visible_articles = [for (a in articles
                                        where is_probably_visible(a) and has_content(a) and
                                              len(get_inner_text(a, true)) >= 50)
                                   a],

        // If exactly one substantial article, use it
        if (len(visible_articles) == 1) visible_articles[0]
        else if (len(visible_articles) == 0 and len(any_visible_articles) == 1)
            any_visible_articles[0]
        else (
            // Score all candidate containers
            // Returns list of [score, text_len, element] arrays
            let candidates = score_candidates(body, flags),

            if (len(candidates) == 0) (
                // Fallback: check main element
                let main_elem = find_deep(body, "main"),
                if (main_elem != null and is_probably_visible(main_elem) and has_content(main_elem))
                    main_elem
                else body
            )
            else (
                // Sort candidates by score descending (Lambda Bug #23 — now fixed)
                // Use bare for (no brackets) to get flat array result
                let sorted = for (c in candidates order by c[0] desc) c,

                // Find all candidates within 75% of the top score
                let threshold = sorted[0][0] * 0.75,
                let close = [for (c in sorted where c[0] >= threshold) c],

                // Among close candidates, prefer the one with most text
                let best = for (c in close order by c[1] desc) c,
                best[0][2]
            )
        )
    )
)

// ============================================
// Content Cleaning
// ============================================

// Check if tag should be removed entirely
fn should_remove_tag(tag) => tag in REMOVE_TAGS

// Check if element looks like an ad or non-content element
fn is_ad_or_noise(elem) => (
    let match_string = lower(get_match_string(elem)),
    let ad_patterns = ["ad-break", "advertisement", "sponsor", "promo", "banner",
                       "social-share", "newsletter", "signup", "popup", "modal"],
    matches_any(match_string, ad_patterns)
)

// Check if element is a share element
fn is_share_element(elem) => (
    let match_string = lower(get_match_string(elem)),
    matches_any(match_string, SHARE_PATTERNS)
)

// Check if element is a data table (vs layout table)
fn is_data_table(table) => (
    let role = get_attr(table, "role", ""),
    if (role == "presentation") false
    else if (get_attr(table, "datatable", "") == "0") false
    else (
        let has_summary = get_attr(table, "summary", null) != null,
        let captions = find_all(table, "caption"),
        let has_caption = len(captions) > 0 and len(captions[0]) > 0,
        let ths = find_all(table, "th"),
        let has_th = len(ths) > 0,
        let cols = find_all(table, "col"),
        let colgroups = find_all(table, "colgroup"),
        let tfoots = find_all(table, "tfoot"),
        let theads = find_all(table, "thead"),
        let has_table_markers = len(cols) > 0 or len(colgroups) > 0 or len(tfoots) > 0 or len(theads) > 0,

        if (has_summary or has_caption or has_th or has_table_markers) true
        else (
            // Check for nested tables (indicates layout)
            let nested = find_all(table, "table"),
            // nested includes self, so > 1 means there's a nested table
            if (len(nested) > 1) false
            else (
                // Count rows and columns
                let rows = find_all(table, "tr"),
                let row_count = len(rows),
                if (row_count == 0) false
                else (
                    let first_row_cells = if (len(rows) > 0) (
                        let tds = find_all(rows[0], "td"),
                        let ths2 = find_all(rows[0], "th"),
                        len(tds) + len(ths2)
                    ) else 0,
                    // Simple heuristic
                    if (row_count == 1 or first_row_cells <= 1) false
                    else if (row_count >= 10 or first_row_cells > 4) true
                    else row_count * first_row_cells > 10
                )
            )
        )
    )
)

// Clean conditionally - port of _cleanConditionally
// Returns true if the element should be removed
fn should_clean_conditionally(elem, flags) => (
    if ((flags div FLAG_CLEAN_CONDITIONALLY) % 2 == 0) false
    else (
        let tag = tag_of(elem),
        let is_list = tag == 'ul' or tag == 'ol',

        // Don't remove data tables
        if (tag == 'table' and is_data_table(elem)) false
        // Don't remove if inside a data table
        else (
            let weight = get_class_weight(elem, flags),
            let content_score = 0,

            if (weight + content_score < 0) true
            else (
                let text = get_inner_text(elem, true),
                let comma_count = count_commas(text),
                if (comma_count >= 10) false
                else (
                    // Detailed checks
                    let ps = find_all(elem, "p"),
                    let imgs = find_all(elem, "img"),
                    let lis = find_all(elem, "li"),
                    let inputs = find_all(elem, "input"),
                    let embeds0 = find_all(elem, "embed"),
                    let objects = find_all(elem, "object"),
                    let iframes = find_all(elem, "iframe"),
                    let embeds = embeds0 ++ objects ++ iframes,

                    let p_count = len(ps),
                    let img_count = len(imgs),
                    let li_count = len(lis) - 100,     // subtract 100 like readability.js
                    let input_count = len(inputs),

                    let headings = collect_by_tags(elem, ['h1', 'h2', 'h3', 'h4', 'h5', 'h6']),
                    let heading_text = sum([for (h in headings) len(get_inner_text(h, true))]),
                    let total_text = len(text),
                    let heading_density = if (total_text > 0) heading_text * 1.0 / (total_text * 1.0) else 0.0,

                    // Filter embeds to non-video ones
                    let non_video_embeds = [for (e in embeds) (
                        let src = get_attr(e, "src", ""),
                        let is_video = matches_any(src, VIDEO_SITES),
                        if (is_video) null else e
                    )],
                    let embed_count = len([for (e in non_video_embeds where e != null) e]),

                    let content_length = total_text,
                    let link_density = get_link_density(elem),

                    let text_density_tags = ['span', 'li', 'td'] ++ DIV_TO_P_ELEMS,
                    let text_density = get_text_density(elem, text_density_tags),

                    // Check if inside a figure
                    // (simplified - check tag of parent equivalent)
                    let is_figure = tag == 'figure',

                    // Shadiness checks (any triggers removal)
                    let bad_img_ratio = not is_figure and img_count > 1 and (
                        if (img_count > 0) (p_count * 1.0 / (img_count * 1.0)) < 0.5
                        else false
                    ),
                    let too_many_lis = not is_list and li_count > p_count,
                    let too_many_inputs = input_count > floor(p_count / 3),
                    let suspiciously_short = not is_list and not is_figure and
                                             heading_density < 0.9 and content_length < 25 and
                                             (img_count == 0 or img_count > 2) and
                                             link_density > 0.0,
                    let low_weight_linky = not is_list and weight < 25 and link_density > 0.2,
                    let high_weight_linky = weight >= 25 and link_density > 0.5,
                    let suspicious_embed = (embed_count == 1 and content_length < 75) or embed_count > 1,
                    let no_useful_content = img_count == 0 and text_density == 0.0,

                    bad_img_ratio or too_many_lis or too_many_inputs or
                    suspiciously_short or low_weight_linky or high_weight_linky or
                    suspicious_embed or no_useful_content
                )
            )
        )
    )
)

// Get cleaned text content from an element tree (recursive)
fn get_clean_text(elem) =>
    if (elem == null) ""
    else if (elem is string) elem
    else if (not (elem is element)) string(elem)
    else (
        let tag = tag_of(elem),
        if (tag in REMOVE_TAGS) ""
        else if (is_ad_or_noise(elem)) ""
        else (
            let n = len(elem),
            let parts = [for (i in 0 to n - 1) get_clean_text(elem[i])],
            str_join(parts, " ")
        )
    )

// ============================================
// Public API Functions
// ============================================

/// Parse HTML file and extract readable content.
/// Uses file path string: readability.parse("article.html")
pub fn parse(file_path) map^ => parse_doc(input(file_path, 'html')?)

// Temporary debug function to inspect scoring
pub fn debug_scoring_info(doc) => (
    let body = find_deep(doc, "body"),
    let flags = 7,
    let candidates = score_candidates(body, flags),
    let num = len(candidates),
    // Direct access without sorting (avoid Lambda Bug #23)
    let max_score = if (num > 0) max([for (s in candidates) s[0]]) else 0,
    let best = [for (s in candidates where s[0] == max_score) s],
    let best_elem = if (len(best) > 0) best[0][2] else null,
    let best_cls = if (best_elem != null) get_attr(best_elem, "class", "") else "",
    let best_id = if (best_elem != null) get_attr(best_elem, "id", "") else "",
    let best_preview = if (best_elem != null) slice(get_inner_text(best_elem, true), 0, 100) else "",
    {total: num, best_score: max_score, best_cls: best_cls, best_id: best_id, preview: best_preview}
)

pub fn debug_excerpt_info(doc) => (
    let result = parse_doc(doc),
    let content = result.content,
    let content_tag = if (content != null) tag_of(content) else 'null',
    let content_children = if (content != null) len(content) else -1,
    // Check how many elements collect_all_elements finds
    let all_elems = if (content != null) collect_all_elements(content) else [],
    let elem_count = len(all_elems),
    // Check p elements
    let p_count = len([for (e in all_elems where tag_of(e) == 'p') e]),
    // Check all para_likes
    let para_count = len([for (e in all_elems
                              where tag_of(e) == 'p' or
                                    tag_of(e) == 'pre' or
                                    is_div_as_paragraph(e)) e]),
    // Check text of first 3 para_likes
    let paras = [for (e in all_elems
                      where tag_of(e) == 'p' or
                            tag_of(e) == 'pre' or
                            is_div_as_paragraph(e)) e],
    let first_texts = [for (i in 0 to min(len(paras), 3) - 1,
                            let p = paras[i],
                            let txt = get_inner_text(p, false))
                       {idx: i, tag: tag_of(p), text_len: len(txt), preview: slice(txt, 0, 80)}],
    {
        content_tag: content_tag,
        content_children: content_children,
        elem_count: elem_count,
        p_count: p_count,
        para_count: para_count,
        first_paras: first_texts,
        excerpt: result.excerpt,
        byline: result.byline
    }
)

/// Parse HTML document object.
/// Core implementation of the readability algorithm.
pub fn parse_doc(doc) => (
    let json_ld = get_json_ld(doc),
    let metadata = extract_metadata(doc, json_ld),
    let lang = get_lang(doc),
    let dir = get_dir(doc),

    // Try extraction with all flags active, then retry with fewer flags
    let flags = FLAG_STRIP_UNLIKELYS + FLAG_WEIGHT_CLASSES + FLAG_CLEAN_CONDITIONALLY,
    try_extract(doc, metadata, flags, lang, dir, [])
)

// Try extraction with given flags, retry with fewer if content is too short
fn try_extract(doc, metadata, flags, lang, dir, attempts) => (
    let article_content = find_best_candidate(doc, flags),

    let text_content = if (article_content != null) (
        let cleaned = get_clean_text(article_content),
        normalize_whitespace(cleaned)
    ) else "",

    let text_len = len(text_content),

    if (text_len >= DEFAULT_CHAR_THRESHOLD) (
        build_result(doc, metadata, article_content, text_content, lang, dir, flags)
    )
    else if (flags > 0) (
        // Retry with fewer flags
        let new_flags = if ((flags div FLAG_STRIP_UNLIKELYS) % 2 == 1)
                            flags - FLAG_STRIP_UNLIKELYS
                        else if ((flags div FLAG_WEIGHT_CLASSES) % 2 == 1)
                            flags - FLAG_WEIGHT_CLASSES
                        else if ((flags div FLAG_CLEAN_CONDITIONALLY) % 2 == 1)
                            flags - FLAG_CLEAN_CONDITIONALLY
                        else 0,
        try_extract(doc, metadata, new_flags, lang, dir, [])
    )
    else
        // All flags exhausted - use current attempt as last resort
        build_result(doc, metadata, article_content, text_content, lang, dir, flags)
)

// Build the final result map
fn build_result(doc, metadata, article_content, text_content, lang, dir, flags) => (
    // Try to extract byline from body if not in metadata
    // Readability.js finds bylines during _grabArticle walk, with behavior
    // depending on whether unlikelies are being stripped:
    // - First pass (strip_unlikelies=true): skips unlikely containers
    // - Retry (strip_unlikelies=false): searches all elements
    let strip_unlikelies = (flags div FLAG_STRIP_UNLIKELYS) % 2 == 1,
    let article_byline = if (metadata.byline != null) metadata.byline
                         else find_byline_in_body(doc, strip_unlikelies)
                         ,

    let excerpt = if (metadata.excerpt != null) metadata.excerpt
                  else if (article_content != null) (
                      // Find visible paragraph-like elements in document order
                      // collect_all_elements already skips hidden subtrees
                      let all_elems = collect_all_elements(article_content),
                      let para_likes = [for (e in all_elems
                                            where tag_of(e) == 'p' or
                                                  tag_of(e) == 'pre' or
                                                  is_div_as_paragraph(e))
                                        e],
                      // Filter: skip paragraphs whose text matches the detected byline
                      // (Readability.js removes byline elements during _grabArticle,
                      // so they never appear in excerpt candidates)
                      let byline_lower = if (article_byline != null) lower(article_byline) else "",
                      let has_byline = len(byline_lower) > 0,
                      let with_text = [for (p in para_likes,
                                          let pt = get_inner_text(p, false),
                                          let has_text = len(pt) > 0,
                                          let is_byline = if (has_byline) contains(lower(pt), byline_lower) else false
                                          where has_text and not is_byline)
                                      pt],
                      if (len(with_text) > 0) with_text[0]
                      else null
                  )
                  else null,

    {
        title: metadata.title,
        byline: article_byline,
        dir: dir,
        content: article_content,
        textContent: text_content,
        length: len(text_content),
        excerpt: excerpt,
        siteName: metadata.siteName,
        lang: lang,
        publishedTime: metadata.publishedTime
    }
)

// Helper to find byline in the document body
// strip_unlikelies: if true, only search elements outside unlikely containers
// When false, search all elements (matches Readability.js retry behavior)
fn find_byline_in_body(doc, strip_unlikelies) => (
    let body = find_deep(doc, "body"),
    if (body == null) null
    else (
        let all_elems = if (strip_unlikelies)
                            collect_elements_skip_unlikely(body)
                        else collect_all_elements(body),
        let byline_elems = [for (e in all_elems where is_valid_byline(e)) e],
        if (len(byline_elems) > 0) extract_byline_text(byline_elems[0])
        else if (strip_unlikelies) (
            // Fallback: check rel="author" across all elements
            // In Readability.js, rel="author" bylines inside unlikely containers
            // are found on retry passes where strip_unlikelies is false.
            // Since our content scoring may succeed on the first pass where
            // Readability.js would retry, check rel="author" as a targeted fallback.
            let all_body_elems = collect_all_elements(body),
            let rel_author_elems = [for (e in all_body_elems
                                        where get_attr(e, "rel", "") == "author")
                                   e],
            let valid_rel = [for (e in rel_author_elems where is_valid_byline(e)) e],
            if (len(valid_rel) > 0) extract_byline_text(valid_rel[0])
            else null
        )
        else null
    )
)

// Extract byline text from a byline element, preferring itemprop="name" descendants
fn extract_byline_text(byline_elem) => (
    let all_desc = collect_all_elements(byline_elem),
    let name_descs = [for (d in all_desc
                           where lower(get_attr(d, "itemprop", "")) == "name")
                      d],
    if (len(name_descs) > 0) trim(get_text(name_descs[0]))
    else trim(get_text(byline_elem))
)

/// Parse HTML file and extract readable content (alias for parse)
pub fn parse_file(file_path) map^ => parse(file_path)

/// Parse and save clean content to file.
/// NOTE: Requires procedural execution (./lambda.exe run)
//pub pn parse_and_save(input_path, output_path) map^ {
//    var result^err = parse(input_path)
//    if (err != null) { raise err }
//    if (result.content != null) {
//        var out^out_err = output(result.content, output_path, 'html')
//        if (out_err != null) { raise out_err }
//    }
//    return result
//}

/// Extract just the article content element
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
    let doc = input(file_path, 'html')?
    is_doc_readable(doc, min_content_length, min_score)
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

        let good_nodes = [for (node in nodes,
                          let is_visible = is_probably_visible(node),
                          let is_likely = not is_unlikely_candidate(node),
                          let text_len = len(trim(get_text(node)))
                          where is_visible and is_likely and text_len >= min_len)
                         sqrt(text_len * 1.0 - min_len * 1.0)],

        let total_score = sum(good_nodes),
        total_score > min_sc * 1.0
    )
)

/// Ensure doc is parsed from file path or already an element
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

/// Get text similarity score between two strings (exposed for testing)
pub fn similarity(a, b) => text_similarity(a, b)

/// Debug: get element name
pub fn elem_name(elem) {
    let tag = if (elem is element) name(elem) else null
    tag
}

/// Debug: get meta value
pub fn debug_meta(doc, key) => get_meta(doc, key)

/// Debug: get metadata
pub fn debug_metadata(doc) => extract_metadata(doc, null)

/// Debug: get raw JSON-LD data
pub fn debug_json_ld(doc) => (
    let head = find_deep(doc, "head"),
    let body = find_deep(doc, "body"),
    let head_scripts = if (head != null) find_all(head, "script") else [],
    let body_scripts = if (body != null) find_all(body, "script") else [],
    let all_scripts = head_scripts ++ body_scripts,
    let json_ld_scripts = [for (s in all_scripts
                                where get_attr(s, "type", "") == "application/ld+json")
                           s],
    let num_scripts = len(json_ld_scripts),
    let script_info = [for (i in 0 to num_scripts - 1) (
        let s = json_ld_scripts[i],
        let n = len(s),
        let child0 = if (n > 0) s[0] else null,
        let is_str = child0 is string,
        let is_el = is_elem(child0),
        let is_lst = child0 is list,
        let dp = child0.datePublished,
        let ctx = child0["@context"],
        let typ = child0["@type"],
        let str_preview = if (is_str and len(child0) > 80) slice(child0, 0, 80)
                          else if (is_str) child0
                          else null,
        {idx: i, n: n, is_str: is_str, is_el: is_el, is_lst: is_lst,
         dp: dp, ctx: ctx, typ: typ, str_preview: str_preview}
    )],
    {num_scripts: num_scripts, scripts: script_info}
)

/// Debug: get first meta
pub fn debug_first_meta(doc) => (
    let keys = ["dc:description", "dcterm:description", "og:description",
                "weibo:article:description", "weibo:webpage:description",
                "description", "twitter:description"],
    get_first_meta(doc, keys)
)
/// Debug: trace article finding
pub fn debug_find_article(doc) => (
    let body = find_deep(doc, "body"),
    if (body == null) {body_found: false}
    else (
        let articles = find_all(body, "article"),
        let article_info = [for (a in articles)
            {visible: is_probably_visible(a),
             content: has_content(a),
             text_len: len(get_inner_text(a, true))}],
        let main_elem = find_deep(body, "main"),
        let main_visible = if (main_elem != null) is_probably_visible(main_elem) else null,
        let main_text = if (main_elem != null) get_inner_text(main_elem, true) else "",
        let main_clean = if (main_elem != null) get_clean_text(main_elem) else "",
        let all_from_body = collect_all_elements(body),
        let para_likes = [for (e in all_from_body
                             where tag_of(e) == 'p' or is_div_as_paragraph(e)) e],
        {body_found: true, num_articles: len(articles), article_info: article_info,
         main_found: main_elem != null, main_visible: main_visible,
         main_text_len: len(main_text), main_clean_len: len(main_clean),
         main_clean: main_clean,
         body_elems: len(all_from_body), para_likes: len(para_likes)}
    )
)
