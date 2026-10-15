import pdf: lambda.pdf.output
import resolve: lambda.pdf.resolve
import radiant
import schema: lambda.pdf.view_schema

fn make_pdf() element^ => pdf.render("test/html/pdf_lambda_phase6.html", {width: 300, height: 260})^

fn effect_event(event, node, ctx) => {content: "", resources:
    if (event == "begin_effect" or event == "end_effect") [{phase: event, opacity: ctx.opacity}] else []}

pn main() {
    let file = make_pdf()^;
    let written = output(file, "temp/pdf_lambda_phase6.pdf", {atomic: true})^;
    let parsed = input("temp/pdf_lambda_phase6.pdf", 'pdf')^;
    let info = resolve.deref(parsed, parsed.trailer.dictionary.Info);
    let catalog = resolve.deref(parsed, parsed.trailer.dictionary.Root);
    let outlines = resolve.deref(parsed, catalog.Outlines);
    let chapter = resolve.deref(parsed, outlines.First);
    let section = resolve.deref(parsed, chapter.First);
    let annotations = [for (entry in parsed.pages[0].dict.Annots) resolve.deref(parsed, entry)];
    let resources = resolve.page_resources(parsed, parsed.pages[0]);
    let scoped = radiant.pdf_walk_source("<html><body><div style='opacity:.5;width:20px;height:20px'><div style='opacity:.25;width:10px;height:10px;background:red'></div></div></body></html>",
        {}, effect_event, schema.schemas())^;
    let begins = [for (event in scoped.resources where event.phase == "begin_effect") event.opacity];
    let endings = [for (event in scoped.resources where event.phase == "end_effect") event.opacity];
    return [written > 1000, len(parsed.pages) == 1, len(info.Title) > 0, info.Author == "Lambda",
        info.Subject == "Functional graphics", outlines.Count == 2, len(chapter.Title) > 0,
        len(section.Title) > 0, section.Parent == outlines.First, len(annotations) >= 2,
        len(resources.XObject) >= 3, len(catalog.Names.Dests.Names) >= 4, begins == [0.5, 0.25], endings == [0.25, 0.5]]
}
