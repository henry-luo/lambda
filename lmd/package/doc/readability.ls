// lambda.doc.readability is an explicit source-package facade (D7.2.4v3/D7.2.6):
// a port of Mozilla Readability.js 0.6.0 (vibe/Lambda_Pkg_Readability.md).
import article: .readability.article
import reader: .readability.readerable

fn root_of(source) element^ => if (source is string) lambda.sys.parse(source, 'html')^ else source

// Readability.parse(): the article of an HTML string or parsed document, or null
pub fn parse(source, options = null) map?^ {
    let root = root_of(source)^;
    article.extract(root, article.html_of(root), options)^
}

// isProbablyReaderable: whether parse() is likely to find an article
pub fn readerable(source, options = null) bool^ => reader.check(article.html_of(root_of(source)^), options)

// read and parse an HTML file; options.url resolves relative links
pub pn read(path, options = null) map?^ {
    let doc = input(path, 'html')^
    return parse(doc, options)^
}
