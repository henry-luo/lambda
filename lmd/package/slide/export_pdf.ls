// Export a .slides deck to one PDF, a page per slide at its final build, written next to the deck.
// `lambda run` takes no script arguments, so this is a function for another script or the REPL:
//     import export: lambda.slide.export_pdf
//     export.export_pdf("talks/deck.slides")^    // writes talks/deck.pdf, returns its path
// Each slide is laid out as a static page and rendered by this same executable (`lambda render`);
// the pages are joined with `pdfunite` (poppler), which Lambda has no native equivalent of.
// Staging files stay in ./temp/slide_export/<deck name>/.
import slide: lambda.slide
import html: lambda.slide.html

// the slide stage is the whole page: no player chrome, letterboxing or page margin
let PAGE_CSS = "html, body { margin: 0; padding: 0; background: #ffffff; } .slide-stage { margin: 0; }"

fn base_name(path) { let parts = split(path, "/"); parts[len(parts) - 1] }
fn dir_part(path) => slice(path, 0, len(path) - len(base_name(path)))
fn stem(file) => if (ends_with(lower(file), ".slides")) slice(file, 0, len(file) - len(".slides")) else file
fn tail_line(text) {
    let lines = if (text == null) [] else [for (l in split(trim(string(text)), "\n") where trim(l) != "") l]
    if (len(lines) == 0) "" else lines[len(lines) - 1]
}

// runs a program to completion; a failure to start, a timeout or a nonzero exit becomes one error
// (a caller sees it only through `^`: an unmarked call would discard it and carry on)
pn run(program, args, what) bool^ {
    var failure = null
    var result = null
    io.exec(program, args, {timeout_ms: 180000}) ^ { failure = ^ } ~ { result = ~ }
    if (failure is error) raise error(what ++ ": " ++ failure.message)
    if (result.timed_out) raise error(what ++ ": timed out")
    if (result.exit_code != 0) raise error(what ++ ": exit " ++ string(result.exit_code) ++ ": " ++ tail_line(result.stderr))
    true
}

pub pn export_pdf(deck_path) string^ {
    let deck = input(deck_path, 'slides')^
    // assets resolve against the deck's own folder, whatever the caller's working directory
    let deck_uri = url_resolve("file://" ++ sys.proc.self.cwd# ++ "/", deck_path)^
    let base = url_resolve(deck_uri, ".")^
    let plan = slide.compile(deck, {base_uri: base})^
    let stages = slide.slides(deck, {base_uri: base})^
    let width = string(int(plan.width))
    let height = string(int(plan.height))
    // @page sets the PDF page to the slide's size (the viewport alone adds a margin around it)
    let css = html.css ++ PAGE_CSS ++ " @page { size: " ++ width ++ "px " ++ height ++ "px; margin: 0; }"
    let deck_name = stem(base_name(deck_path))
    let work = "temp/slide_export/" ++ deck_name
    io.mkdir("temp") ^ { null }
    io.mkdir("temp/slide_export") ^ { null }
    io.mkdir(work) ^ { null }
    // sys.proc.self.argv#[0] is the running executable, as in lambda.archive.workers
    let exe = sys.proc.self.argv#[0]
    let pages = [for (i, stage in stages) work ++ "/slide_" ++ (if (i < 9) "0" else "") ++ string(i + 1)]
    for (i, stage in stages) {
        let page = <html lang: "en", <head *[<meta charset: "UTF-8">, <base href: base>, <style css>]>
            <body stage>>
        output(format(page, 'html'), pages[i] ++ ".html", {atomic: true})^
        run(exe, ["render", "--no-log", pages[i] ++ ".html", "-vw", width, "-vh", height, "-o", pages[i] ++ ".pdf"],
            "slide export: render slide " ++ string(i + 1))^
    }
    let out = dir_part(deck_path) ++ deck_name ++ ".pdf"
    // join into a sibling file first, so a failed merge never replaces an earlier export
    let part = out ++ ".part"
    var failure = null
    run("pdfunite", [*[for (p in pages) p ++ ".pdf"], part],
        "slide export: joining the pages needs pdfunite (poppler); the per-slide PDFs are in " ++ work) ^ { failure = ^ }
    if (failure is error) {
        if (exists(part)) { io.delete(part)^ }
        raise failure
    }
    io.rename(part, out)^
    out
}
