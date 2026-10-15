import pdf: lambda.doc.pdf.output
import radiant
import schema: lambda.doc.pdf.view_schema

fn generate(source) element^ => pdf.render(source, {width: 80, height: 60})^

// Generation runs at module scope, before any procedural file operation.
let source = <html <head <style "body { margin: 0; background: #fff; } div { width: 20px; height: 15px; background: #f00; }">
    <script "throw new Error('PDF generation must not execute scripts');">> <body <div>>>;
let first = generate(source)^;
let second = generate(source)^;
let bytes = binary(first);
let from_path = generate("test/html/pdf_lambda_views.html")^;

fn nested_visit(event, node, ctx) any^ {
    if (event == "begin_page") {
        let before = ctx.page_width;
        let inner = generate(source)^;
        if (len(binary(inner)) > 300 and ctx.page_width == before) "N"
        else raise error("nested PDF generation invalidated the outer view")
    }
    else ""
}

fn count_view(event, node, ctx) => if (event == "element") "E" else ""
let nested_plan = radiant.pdf_walk_source(format(source, 'html')^,
    {width: 80, height: 60}, nested_visit, schema.schemas())^;
let deep_source = "<html><body>" ++ ([for (i in range(0, 80, 1)) "<div>"] |> join("")) ++
    ([for (i in range(0, 80, 1)) "</div>"] |> join("")) ++ "</body></html>";
let deep_plan = radiant.pdf_walk_source(deep_source, {}, count_view, schema.schemas())^;
[first is element, bytes is binary, len(bytes) > 300, bytes == binary(second),
    radiant.pdf_measure(first)^ == len(bytes), len(binary(from_path)) > 400,
    binary(<file format:'pdf', "before" first "after">) == binary("before") ++ bytes ++ binary("after"),
    (pdf.render(42, {}) ^ {true}) == true,
    nested_plan.content == binary("N"), len(deep_plan.content) > 64]
