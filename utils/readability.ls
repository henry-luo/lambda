// Readability - Extract main content from HTML documents
// Port of Mozilla Readability concepts to Lambda Script
//
// Usage:
//   import readability: .utils.readability;
//   let result = readability.parse_file("page.html")
//   // result = {title: "...", content: "...", doc: <html...>}

// ============================================
// String Helper Functions
// ============================================

// Join array of strings with separator (recursive implementation)
fn str_join(arr, sep) =>
    (let n = len(arr),
     if (n == 0) ""
     else if (n == 1) string(arr[0])
     else str_join_at(arr, sep, 1, n, string(arr[0])))

// Helper: join from index with accumulator
fn str_join_at(arr, sep, idx, max_idx, acc) =>
    if (idx >= max_idx) acc
    else str_join_at(arr, sep, idx + 1, max_idx, acc ++ sep ++ string(arr[idx]))

// Extract substring from start to first space or end
fn get_word_at(str, start_idx, max_len) =>
    if (start_idx >= max_len) ""
    else get_word_at_acc(str, start_idx, max_len, "")

fn get_word_at_acc(str, idx, max_len, acc) =>
    if (idx >= max_len) acc
    else (let c = str[idx],
          if (c == " " or c == "\n" or c == "\t" or c == ">") acc
          else get_word_at_acc(str, idx + 1, max_len, acc ++ c))

// ============================================
// Element Helper Functions
// ============================================

// Check if element is an element type (not string or null)
fn is_element(elem) => elem is element

// Get tag name from element by parsing string representation
// string(elem) gives "<tagname ..." or "<tagname>" so we extract tagname
fn get_tag(elem) =>
    if (not is_element(elem)) ""
    else (let s = string(elem),
          let n = len(s),
          if (n < 2) ""
          else if (s[0] != "<") ""
          else get_word_at(s, 1, n))

// Check if element has a specific tag
fn has_tag(elem, tag_name) => get_tag(elem) == tag_name

// Get text content from an element (simple recursive)
fn get_text(elem) =>
    if (elem is string) elem
    else if (elem == null) ""
    else if (not is_element(elem)) ""
    else (let n = len(elem),
          if (n == 0) ""
          else if (n == 1) get_text(elem[0])
          else get_text_at(elem, 1, n, get_text(elem[0])))

// Helper: accumulate text from children
fn get_text_at(elem, idx, max_idx, acc) =>
    if (idx >= max_idx) acc
    else get_text_at(elem, idx + 1, max_idx, acc ++ get_text(elem[idx]))

// ============================================
// Element Search Functions
// ============================================

// Find first child element with matching tag (non-recursive)
fn find_child(elem, tag_name) =>
    if (elem == null) null
    else if (not is_element(elem)) null
    else (let n = len(elem),
          if (n == 0) null
          else find_child_at(elem, tag_name, 0, n))

// Helper: search children at index
fn find_child_at(elem, tag_name, idx, max_idx) =>
    if (idx >= max_idx) null
    else (let child = elem[idx],
          if (has_tag(child, tag_name)) child
          else find_child_at(elem, tag_name, idx + 1, max_idx))

// Find first element with matching tag (deep recursive search)
fn find_deep(elem, tag_name) =>
    if (elem == null) null
    else if (not is_element(elem)) null
    else if (get_tag(elem) == tag_name) elem
    else (let n = len(elem),
          if (n == 0) null
          else find_deep_at(elem, tag_name, 0, n))

// Helper: deep search through children
fn find_deep_at(elem, tag_name, idx, max_idx) =>
    if (idx >= max_idx) null
    else (let child = elem[idx],
          let result = find_deep(child, tag_name),
          if (result != null) result
          else find_deep_at(elem, tag_name, idx + 1, max_idx))

// Collect all matching elements (returns array, collects by recursion)
fn collect_all(elem, tag_name, acc) =>
    if (elem == null) acc
    else if (not is_element(elem)) acc
    else if (get_tag(elem) == tag_name) acc ++ [elem]
    else (let n = len(elem),
          if (n == 0) acc
          else collect_all_at(elem, tag_name, 0, n, acc))

// Helper: iterate through children collecting matches
fn collect_all_at(elem, tag_name, idx, max_idx, acc) =>
    if (idx >= max_idx) acc
    else (let child = elem[idx],
          let new_acc = collect_all(child, tag_name, acc),
          collect_all_at(elem, tag_name, idx + 1, max_idx, new_acc))

// ============================================
// Content Extraction
// ============================================

// Extract title from document
fn extract_title(doc) =>
    (let head = find_deep(doc, "head"),
     let title_elem = if (head != null) find_deep(head, "title") else null,
     if (title_elem != null) trim(get_text(title_elem))
     else "")

// Extract all paragraphs from body as array
fn extract_paragraphs(doc) =>
    (let body = find_deep(doc, "body"),
     if (body != null) collect_all(body, "p", [])
     else [])

// Extract text from all paragraphs and join
fn extract_text(doc) =>
    (let paragraphs = extract_paragraphs(doc),
     let texts = (for (p in paragraphs) trim(get_text(p))),
     let non_empty = (for (t in texts) if (len(t) > 0) t else ""),
     str_join(non_empty, "\n\n"))

// Find main content element (article, main, or body)
fn find_content_root(doc) =>
    (let body = find_deep(doc, "body"),
     if (body == null) null
     else (let article = find_deep(body, "article"),
           if (article != null) article
           else (let main_elem = find_deep(body, "main"),
                 if (main_elem != null) main_elem
                 else body)))

// ============================================
// Main Readability Function (Public API)
// ============================================

// Parse HTML string and extract readable content
pub fn parse(html_content) =>
    (let doc^err = input(html_content, 'html'),
     if (err != null) null
     else (let title = extract_title(doc),
           let content = extract_text(doc),
           {title: title, content: content, doc: doc}))

// Parse HTML file and extract readable content
pub fn parse_file(file_path) =>
    (let doc^err = input(file_path, 'html'),
     if (err != null) {title: null, content: null, doc: null}
     else (let title = extract_title(doc),
           let content = extract_text(doc),
           {title: title, content: content, doc: doc}))

// Extract only the title from an HTML document
pub fn title(doc) => extract_title(doc)

// Extract only the text content from an HTML document  
pub fn text(doc) => extract_text(doc)

// Get all paragraphs from an HTML document
pub fn paragraphs(doc) => extract_paragraphs(doc)

// Find the main content root element (article, main, or body)
pub fn content_root(doc) => find_content_root(doc)

// ============================================
// Test
// ============================================

// Test with local file
let result = parse_file("./test/input/test.html")
{
    title: result.title,
    content: result.content
}