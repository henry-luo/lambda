// view.ls — view-only projections of content the editor keeps but cannot edit
// (vibe/radiant/Radiant_Design_Edit_Mode.md §2).
//
// A part the editor cannot edit reads as it renders, never as editable text:
// Markdown through the Markdown vocabulary, raw or retained HTML through a
// sanitizing writer. A projection is display only — no script runs, no style
// reaches the page, no link navigates, no control acts — and is never read
// back: a save writes the part's source.
//
// Math shows its TeX source. The edit application runs on the MIR tier (its
// templates handle events), so every module it imports is compiled, and
// compiling the math package's metrics data costs seconds and gigabytes; a
// math view waits for that cost to go.
//
// Element literals need a static tag, so the writers below build HTML text
// and parse it back into elements for the surface.

import .model

// ---------------------------------------------------------------------------
// HTML: a sanitizing writer
// ---------------------------------------------------------------------------

// Tag and attribute names are compared as strings here (a string never
// equals a symbol), since the writers lower-case what they read.

// Elements that act on or restyle the page rather than show content.
let inert_tags = ["script", "style", "link", "meta", "base", "title", "head", "template", "noscript"]
// Embedded browsing contexts are named, never loaded.
let embed_tags = ["iframe", "frame", "frameset", "object", "embed", "applet"]
let void_tags = ["area", "br", "col", "hr", "img", "input", "source", "track", "wbr"]
let control_tags = ["input", "button", "select", "textarea", "fieldset"]
// Attributes that act: navigation, submission, inline documents.
let acting_attrs = ["href", "action", "formaction", "srcdoc", "ping"]

fn script_url(value) => starts_with(lower(trim(value)), "javascript:") or false

fn attr_html(name, value) => " " ++ name ++ "=\"" ++ escape_attr(value) ++ "\""

// A link shows its target as a tooltip; a control is disabled.
fn safe_attrs(el, tag) {
  // an empty attribute value reads back as null
  let pairs = [for (k, v at map(el)) {name: lower(string(k)), value: if (v == null) "" else string(v)}]
  let kept = [for (p in pairs where not (starts_with(p.name, "on") or member(acting_attrs, p.name) or
                                          contains(p.name, " ") or script_url(p.value))) p]
  let href = [for (p in pairs where p.name == "href") p.value]
  let titled = any([for (p in kept) p.name == "title"]) or false
  let link_title = if (len(href) > 0 and not titled) attr_html("title", href[0]) else ""
  let off = any([for (p in kept) p.name == "disabled"]) or false
  let disabled = if (member(control_tags, tag) and not off) attr_html("disabled", "disabled") else ""
  join([for (p in kept) attr_html(p.name, p.value)], "") ++ link_title ++ disabled
}

// HTML text for parsed HTML items (an element, a run, or text), without what
// acts: scripts, styles, frames, handlers, navigation.
pub fn safe_html(item) {
  if (type(item) == string) { escape_text(item) }
  else if (type(item) == array or type(item) == list) { join([for (x in item) safe_html(x)], "") }
  else if (type(item) != element) { "" }
  else {
    let tag = lower(string(name(item)))
    // comments, doctypes and processing instructions carry no view
    if (starts_with(tag, "#") or starts_with(tag, "!") or starts_with(tag, "?") or member(inert_tags, tag)) { "" }
    else if (member(embed_tags, tag)) { "<span class=\"edit-view-embed\">&lt;" ++ escape_text(tag) ++ "&gt;</span>" }
    else if (member(void_tags, tag)) { "<" ++ tag ++ safe_attrs(item, tag) ++ ">" }
    else { "<" ++ tag ++ safe_attrs(item, tag) ++ ">" ++ safe_html(content(item)) ++ "</" ++ tag ++ ">" }
  }
}

fn first_named(items, tag) {
  let found = [for (c in items where type(c) == element and name(c) == tag) c]
  if (len(found) > 0) found[0] else null
}

// The body content of a parsed HTML document.
fn body_items(doc) {
  let html = if (doc == null) null else first_named(content(doc), 'html')
  let body = if (html == null) null else first_named(content(html), 'body')
  if (body == null) [] else [for (c in content(body)) c]
}

// The items of an HTML fragment.
fn parsed_body(html_text) =>
  if (trim(html_text) == "") [] else body_items(parse("<body>" ++ html_text ++ "</body>", 'html') ^ { null })

// Elements that show something without text content.
let replaced_tags = ["img", "input", "br", "hr", "video", "audio", "canvas", "svg", "math", "picture",
                     "meter", "progress", "select", "textarea", "button"]

fn shows_something(item) {
  if (type(item) == string) { trim(item) != "" }
  else if (type(item) == array or type(item) == list) { any([for (x in item) shows_something(x)]) or false }
  else if (type(item) != element) { false }
  else { member(replaced_tags, lower(string(name(item)))) or shows_something(content(item)) }
}

// The view of retained HTML: parsed items (the HTML adapter) or source text
// (Markdown's raw HTML). A part that shows nothing on its own — a lone tag,
// a comment — has no view.
pub fn html_view(source) {
  let items = parsed_body(safe_html(if (type(source) == string) parsed_body(source) else source))
  if (shows_something(items)) items else []
}

// ---------------------------------------------------------------------------
// Markdown: the parser's vocabulary as HTML
// ---------------------------------------------------------------------------

// Parser tags that are HTML tags of the same meaning.
let md_html_tags = ["p", "h1", "h2", "h3", "h4", "h5", "h6", "blockquote", "ul", "ol", "li", "table",
                    "thead", "tbody", "tfoot", "tr", "th", "td", "strong", "b", "em", "i", "del", "s",
                    "strike", "sup", "sub", "dl", "dt", "dd", "div", "mark", "ins", "kbd", "abbr"]

fn md_attrs(item, tag) {
  let align = if (tag == "th" or tag == "td") item.align else null
  let start = if (tag == "ol") item.start else null;
  (if (align == null) "" else attr_html("style", "text-align: " ++ string(align))) ++
  (if (start == null) "" else attr_html("start", string(start)))
}

fn md_children(item) => join([for (c in content(item)) md_html(c)], "")

fn md_html(item) {
  if (type(item) == string) { escape_text(item) }
  // the parser keeps a :name: emoji shortcode as a bare symbol
  else if (type(item) == symbol) { escape_text(":" ++ string(item) ++ ":") }
  else if (type(item) != element) { "" }
  else {
    let tag = string(name(item))
    if (tag == "span") { md_children(item) }
    else if (tag == "softbreak") { " " }
    else if (tag == "br") { "<br>" }
    else if (tag == "hr") { "<hr>" }
    else if (tag == "code" and item.type == "block") { "<pre><code>" ++ escape_text(plain_text(item)) ++ "</code></pre>" }
    else if (tag == "code") { "<code>" ++ escape_text(plain_text(item)) ++ "</code>" }
    // math reads as its TeX source (see the header)
    else if (tag == "math" and item.type == "block") {
      "<div class=\"edit-view-source\">$$" ++ escape_text(plain_text(item)) ++ "$$</div>"
    }
    else if (tag == "math") { "<span class=\"edit-view-source\">$" ++ escape_text(plain_text(item)) ++ "$</span>" }
    // written as read: markdown_view sanitizes the whole block
    else if (tag == "raw-html" or tag == "html-block") { plain_text(item) }
    else if (tag == "footnote-ref") { "<sup class=\"edit-view-note\">[" ++ escape_text(string(item.ref)) ++ "]</sup>" }
    else if (tag == "a") { "<a" ++ attr_html("title", string(item.href or "")) ++ ">" ++ md_children(item) ++ "</a>" }
    else if (tag == "img") {
      "<img" ++ attr_html("src", string(item.src or "")) ++ attr_html("alt", string(item.alt or "")) ++ ">"
    }
    else if (tag == "input") {
      "<input type=\"checkbox\" disabled=\"disabled\"" ++ (if (item.checked != null) " checked=\"checked\"" else "") ++ ">"
    }
    else if (member(md_html_tags, tag)) { "<" ++ tag ++ md_attrs(item, tag) ++ ">" ++ md_children(item) ++ "</" ++ tag ++ ">" }
    // an element the vocabulary does not name reads as its content
    else { md_children(item) }
  }
}

// The view of one parsed Markdown block. Its raw HTML tags are single
// tokens, so the block is sanitized whole: a tag it opens around Markdown
// text closes as the source closes it.
// A block that is only math has no view: its source lines show as written.
pub fn markdown_view(item) => if (name(item) == 'math') [] else html_view(md_html(item))
