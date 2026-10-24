// lambda.mail.mime — what a mail window shows of a message read by
// parse(…, 'eml') or input(…, 'eml') (vibe/Lambda_Demo_Mail.md §4.4, MG5).

// Every leaf part, depth first.
pub fn leaves(part) {
    if (part == null) []
    else if (part.parts != null) [for (child in part.parts, leaf in leaves(child)) leaf]
    else [part]
}

pub fn is_attachment(part) => part.disposition == "attachment" or (part.filename != null and part.disposition != "inline")

fn first_part(message, mime_type) {
    let found = [for (p in leaves(message.mime) where p.type == mime_type and not is_attachment(p)) p];
    if (len(found) == 0) null else found[0]
}

// RFC 3676: a line ending in a space continues on the next one; with
// delsp=yes that space is the sender's, not the text's. The signature
// separator "-- " is never soft.
pub fn unflow(text, delsp) {
    let lines = split(replace(text, "\r\n", "\n"), "\n");
    join([for (i, line in lines) {
        let soft = len(line) > 0 and slice(line, len(line) - 1) == " " and line != "-- ";
        if (soft) (if (delsp) slice(line, 0, len(line) - 1) else line)
        else if (i == len(lines) - 1) line
        else line ++ "\n"
    }], "")
}

// The plain-text body, flowed text unwrapped; null when the message has none.
pub fn text_body(message) {
    let part = first_part(message, "text/plain");
    if (part == null or part.content == null) null
    else if (part.format != null and lower(part.format) == "flowed") unflow(part.content, part.delsp)
    else part.content
}

pub fn html_body(message) {
    let part = first_part(message, "text/html");
    if (part == null) null else part.content
}

pub fn attachments(message) => [for (p in leaves(message.mime) where is_attachment(p))
    {filename: p.filename, type: p.type, size: p.size, content: p.content}]

// Parts an HTML body names by cid: URL (RFC 2392).
pub fn cid_parts(message) => [for (p in leaves(message.mime) where p.cid != null) p]

fn strip_tags(html) => join([for (piece in split(html, "<")) {
    let close = index_of(piece, ">");
    if (close == null) piece else slice(piece, close + 1)
}], " ")

fn collapse(text) => join([for (word in split(replace(replace(replace(text, "\r", " "), "\n", " "), "\t", " "), " ")
    where word != "") word], " ")

fn entity_decoded(text) => replace(replace(replace(replace(replace(replace(text, "&nbsp;", " "), "&lt;", "<"),
    "&gt;", ">"), "&quot;", "\""), "&#39;", "'"), "&amp;", "&")

// the HTML without its <style> and <head> blocks, whose text is not content
fn without_blocks(html) => reduce([html, "style", "head"], (text, tag) => (let pieces = split(text, "<" ++ tag),
    join([for (i, piece in pieces) if (i == 0) piece else (let end = index_of(piece, "</" ++ tag ++ ">"),
        if (end == null) "" else slice(piece, end + len(tag) + 3))], "")))

// An HTML body as plain text: block ends become line breaks, tags go, the
// common entities are decoded, and runs of blank lines shrink to one.
pub fn html_text(html) {
    let marked = reduce([without_blocks(html), "<br", "</p>", "</div>", "</tr>", "</h1>", "</h2>", "</h3>", "</li>", "</table>"],
        (text, tag) => replace(replace(text, tag, "\n" ++ tag), upper(tag), "\n" ++ upper(tag)))
    let lines = [for (line in split(entity_decoded(strip_tags(marked)), "\n")) collapse(line)]
    let kept = [for (i, line in lines where line != "" or (i > 0 and lines[i - 1] != "")) line];
    trim(join(kept, "\n"))
}

// The readable body: the plain-text part, else the HTML part as text.
pub fn readable_text(message) {
    let text = text_body(message);
    if (text != null) text else if (html_body(message) != null) html_text(html_body(message)) else ""
}

// A one-line preview of at most `size` characters, without quoted lines.
pub fn snippet(message, size) {
    let text = text_body(message)
    let body = if (text != null) join([for (line in split(text, "\n") where slice(line, 0, 1) != ">") line], " ")
        else if (html_body(message) != null) html_text(html_body(message))
        else ""
    let flat = collapse(body);
    if (len(flat) <= size) flat else slice(flat, 0, size)
}

// Text for the search sidecar: who, what, and the readable body (§5.4.3).
pub fn search_text(message) {
    let from = if (message.addresses.from == null) [] else message.addresses.from
    let to = if (message.addresses.to == null) [] else message.addresses.to
    let people = join([for (a in [*from, *to]) (if (a.name == null) "" else a.name ++ " ") ++ a.address], ", ")
    join([people, if (message.subject == null) "" else message.subject, readable_text(message)], "\n")
}
