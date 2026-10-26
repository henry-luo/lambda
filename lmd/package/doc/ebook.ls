// Source packages share the lambda.doc namespace (D7.2.4).
import c: .ebook.common
import epub: .ebook.epub
import chapters: .ebook.chapter
import locations: .ebook.location
import presentation: .ebook.reader

fn checked(options) map^ {
    let opts = c.checked_options(options,["rendition","unsupported"])^;
    if (opts.rendition != null and (not (opts.rendition is int) or opts.rendition < 0))
        raise c.fail("invalid-options","rendition must be a nonnegative integer")
    else if (opts.unsupported != null and opts.unsupported != "error")
        raise c.fail("invalid-options","Only unsupported: error is supported")
    else opts
}
pub fn parse(snapshot,options = null) map^ {
    let opts = checked(options)^
    let captured = if (snapshot is binary) lambda.sys.parse(snapshot,"zip")^ else snapshot;
    epub.parse_archive(captured,opts)^
}
pub pn read(path,options = null) map^ {
    let opts = checked(options)^
    if (not (path is string) or path == "" or contains(path,"://"))
        raise c.fail("invalid-source","A local ebook filename is required")
    let snapshot = input(path,"binary")^;
    return parse(snapshot,opts)^
}
pub fn chapter(book,target,options = null) map^ {
    let checked = c.checked_options(options,["highlights"])^;
    chapters.load(book,target,c.get(checked,"highlights",[]))^
}
pub fn locate(book,locator) map^ => locations.resolve(book,locator)^
pub fn locator(book,chapter,offset) map^ => locations.at(book,chapter,offset)^
pub fn search(book,query,options = null) array^ => locations.search(book,query,options)^
pub fn reader(book,options = null) element^ => presentation.reader(book,options)^
