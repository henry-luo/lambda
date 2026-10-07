// Phase IV §9.6: PGFPlots table sources (inline and beside the document) with
// column selection, symbolic x coordinates, and the document-directory gate.
import tikz: lambda.doc.tikz.tikz
import plots: lambda.doc.tikz.pgfplots
import plotdata: lambda.doc.tikz.plotdata
import bridge: lambda.latex.tikz_bridge

let BASE = "test/latex/fixtures/tikz_phase4"

fn children_named(node, tag) => [for (child in node
    where child is element and string(name(child)) == tag) child]

fn r3(v) => float(int(v * 1000.0 + (if (v < 0.0) -0.5 else 0.5))) / 1000.0

fn axis_of(source) any^ => parse(source, {type: "tikz"})^[0]

// Series points as [x, y] (or [x, y, error]) after symbolic mapping.
fn series_points(source, options = null) any^ {
    let plan = plots.resolve_axis(axis_of(source)^, options)^;
    [for (series in plan.series)
        [for (point in series.points) if (point.errors.plus_y > 0.0)
            [r3(point.x), r3(point.y), r3(point.errors.plus_y)] else [r3(point.x), r3(point.y)]]]
}

fn failure(source, options = null) => (series_points(source, options) ^ { ^.message }) ^ { ^.message }

let inline_source = "\\begin{axis}\\addplot table {\n  x y\n  0 1\n  1 3\n  2 2\n};" ++
    "\\addplot table[x=t, y=v] {t u v\n0 9 1\n1 8 0.5\n};" ++
    "\\addplot table[x index=2, y index=0] {5 6 7\n8 9 10\n};" ++
    "\\addplot table[row sep=\\\\, col sep=comma] {a,b\\\\ 1,2\\\\ 3,4\\\\};\\end{axis}"
let inline_tree = axis_of(inline_source)^
let inline_plot = children_named(inline_tree, "plot")[1]

let file_options = {base_uri: BASE}
let file_source = "\\begin{axis}\\addplot+[error bars/.cd, y dir=both, y explicit]" ++
    " table[x=month, y=units, y error=returns] {sales.dat};" ++
    "\\addplot table[col sep=comma, x index=1, y=q2] {regions.csv};\\end{axis}"

let symbolic_source = "\\begin{axis}[ybar, symbolic x coords={north,south,west}, xtick=data," ++
    "enlarge x limits=0.25]" ++
    "\\addplot table[col sep=comma, x=region, y=q1] {regions.csv};" ++
    "\\addplot coordinates {(north,2) (west,1)};\\end{axis}"
let symbolic_html = format(tikz.render_with_program("\\begin{tikzpicture}" ++ symbolic_source ++
    "\\end{tikzpicture}", [], [], 0, false, file_options)^, 'xml')

// A LaTeX graphics island resolves its tables beside the document.
let document = input(BASE ++ "/table_plot.tex", {type: "latex"})^
let islands = [for (child in children_named(document, "document")[0]
    where child is element and string(name(child)) == "tikzpicture") child]
let rendered = [for (island in islands)
    format(bridge.render_picture(island, [], [], [], [], false, BASE), 'xml')]

// A direct .pgf document uses its own directory.
let pgf_html = format(tikz.render_document(parse("\\begin{tikzpicture}" ++ file_source ++
    "\\end{tikzpicture}", {type: "tikz"})^, {source_path: BASE ++ "/figure.pgf"})^, 'xml');

[inline_plot.input_kind, inline_plot.table_source, inline_plot.table_offset,
    [for (option in children_named(children_named(inline_plot, "table_options")[0], "option"))
        option.key ++ "=" ++ option.value]];
series_points(inline_source)^;
series_points(file_source, file_options)^;
series_points(symbolic_source, file_options)^;
[contains(symbolic_html, ">north</text>"), contains(symbolic_html, ">west</text>"),
    len(split(symbolic_html, "tikz-bar")) - 1];
[for (html in rendered) [contains(html, "tikz-axis"), not contains(html, "latex-tikz-unsupported"),
    len(split(html, "tikz-error-bar")) - 1, len(split(html, "tikz-bar")) - 1]];
// y spans 90-2 (a minus error) to 180+9 (a plus error) and the q2 column (2..6).
[contains(pgf_html, "tikz-axis"), contains(pgf_html, "data-y-domain=\"2:189\""),
    contains(pgf_html, "data-x-domain=\"1:4\"")];
[failure("\\begin{axis}\\addplot table {missing.dat};\\end{axis}", file_options),
    failure("\\begin{axis}\\addplot table {../tikz/cycle.tex};\\end{axis}", file_options),
    failure("\\begin{axis}\\addplot table {/etc/hosts};\\end{axis}", file_options),
    failure("\\begin{axis}\\addplot table {sales.dat};\\end{axis}"),
    failure("\\begin{axis}\\addplot table[x=day] {sales.dat};\\end{axis}", file_options),
    failure("\\begin{axis}\\addplot table[y index=7] {sales.dat};\\end{axis}", file_options),
    failure("\\begin{axis}\\addplot table[x expr=\\coordindex] {sales.dat};\\end{axis}", file_options),
    failure("\\begin{axis}\\addplot table[col sep=pipe] {sales.dat};\\end{axis}", file_options),
    failure("\\begin{axis}\\addplot table {a b\nx y\n};\\end{axis}"),
    failure("\\begin{axis}[symbolic x coords={a,b}]\\addplot coordinates {(c,1)};\\end{axis}"),
    failure("\\begin{axis}\\addplot coordinates {(c,1)};\\end{axis}"),
    failure("\\begin{semilogxaxis}[symbolic x coords={a,b}]\\addplot coordinates {(a,1)};" ++
        "\\end{semilogxaxis}"),
    tikz.render("\\begin{tikzpicture}\\begin{axis}[xtick=data]\\addplot coordinates {(1,1)};" ++
        "\\end{axis}\\end{tikzpicture}") ^ { ^.message },
    plotdata.inline_table("data.dat"), plotdata.inline_table("1 2\n3 4")]
