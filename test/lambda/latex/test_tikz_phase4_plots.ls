// Phase IV §9.6: PGFPlots ybar/xbar (grouped and stacked), explicit error bars,
// \closedcycle area plots and fillbetween. Data semantics come from the
// axis plan; geometry is read back from the rendered SVG in axis px.
import tikz: lambda.doc.tikz.tikz
import plots: lambda.doc.tikz.pgfplots

fn r3(v) => float(int(v * 1000.0 + (if (v < 0.0) -0.5 else 0.5))) / 1000.0

fn axis_of(source) any^ => parse(source, {type: "tikz"})^[0]

fn plan_of(source) any^ => plots.resolve_axis(axis_of(source)^)^

fn failure(source) => tikz.render(source) ^ { ^.message }

fn attr_number(tag, key) {
    let at = index_of(tag, " " ++ key ++ "=\"")
    if (at == null) null
    else (let rest = slice(tag, at + len(key) + 3, len(tag)),
        r3(float(slice(rest, 0, index_of(rest, "\"")))))
}

// Rendered marks of one class, in source order.
fn tags(html, class_name) => [for (part in split(html, "<")
    where starts_with(part, "rect class=\"" ++ class_name ++ "\"") or
        starts_with(part, "path class=\"" ++ class_name ++ "\"")) part]

fn rects(html) => [for (tag in tags(html, "tikz-bar"))
    [attr_number(tag, "x"), attr_number(tag, "y"), attr_number(tag, "width"),
     attr_number(tag, "height")]]

fn path_data(tag) {
    let at = index_of(tag, " d=\"")
    let rest = slice(tag, at + 4, len(tag));
    slice(rest, 0, index_of(rest, "\""))
}

fn summary(plan) => {kinds: [for (series in plan.series) series.kind],
    x: [r3(plan.xdomain[0]), r3(plan.xdomain[1])], y: [r3(plan.ydomain[0]), r3(plan.ydomain[1])]}

let grouped_source = "\\begin{axis}[ybar, symbolic x coords={a,b,c}, xtick=data," ++
    "enlarge x limits=0.25]\\addplot coordinates {(a,1) (b,3) (c,2)};" ++
    "\\addplot coordinates {(a,2) (b,1) (c,4)};\\legend{one,two}\\end{axis}"
let grouped = plan_of(grouped_source)^
let grouped_html = format(tikz.render(grouped_source)^, 'xml')

let horizontal_source = "\\begin{axis}[xbar, symbolic y coords={low,high}, ytick=data," ++
    "bar width=6pt, xmin=0, enlarge y limits=0.5]\\addplot coordinates {(3,low) (5,high)};\\end{axis}"
let horizontal = plan_of(horizontal_source)^
let horizontal_html = format(tikz.render(horizontal_source)^, 'xml')

let stacked_source = "\\begin{axis}[ybar stacked, enlarge x limits=0.5]\\addplot coordinates {(1,1) (2,3)};" ++
    "\\addplot coordinates {(1,2) (2,1)};\\end{axis}"
let stacked = plan_of(stacked_source)^
let stacked_html = format(tikz.render(stacked_source)^, 'xml')

let errors_source = "\\begin{axis}\\addplot+[error bars/.cd, y dir=both, y explicit]" ++
    " coordinates {(0,1) +- (0,0.5) (1,2) +- (0,0.25)};" ++
    "\\addplot+[error bars/y dir=plus, error bars/y fixed=0.5, error bars/error mark=none]" ++
    " coordinates {(0,0) (1,1)};\\end{axis}"
let errors = plan_of(errors_source)^
let errors_html = format(tikz.render(errors_source)^, 'xml')

let area_source = "\\begin{axis}[ymin=0]\\addplot[fill=blue] coordinates {(0,1) (1,2) (2,1)}" ++
    " \\closedcycle;\\addplot coordinates {(0,2) (2,2)};\\legend{area,line}\\end{axis}"
let area = plan_of(area_source)^
let area_html = format(tikz.render(area_source)^, 'xml')

let between_source = "\\begin{axis}[domain=0:2, samples=5]\\addplot[name path=A] {x};" ++
    "\\addplot[name path=B] {x^2};\\addplot[gray] fill between[of=A and B," ++
    " soft clip={domain=0:1}];\\path[name path=C] (axis cs:0,3) -- (axis cs:2,3);" ++
    "\\addplot[green] fill between[of=B and C];\\end{axis}"
let between = plan_of(between_source)^
let between_html = format(tikz.render(between_source)^, 'xml');

summary(grouped);
[for (series in grouped.series) [for (point in series.points) [point.x, point.base, point.top]]];
rects(grouped_html);
[contains(grouped_html, ">a</text>"), contains(grouped_html, ">c</text>"),
    len(split(grouped_html, "tikz-legend-entry")) - 1];
summary(horizontal);
rects(horizontal_html);
summary(stacked);
[for (series in stacked.series) [for (point in series.points) [point.x, point.base, point.top]]];
rects(stacked_html);
summary(errors);
[for (series in errors.series) series.errors];
[for (tag in tags(errors_html, "tikz-error-bar")) path_data(tag)];
// One-sided explicit offsets: `+=` sets only the plus error, `-=` only the minus error.
(let sided = plan_of("\\begin{axis}\\addplot+[error bars/.cd, y dir=both, y explicit]" ++
    " coordinates {(0,1) += (0,0.5) (1,2) -= (0,0.25)};\\end{axis}")^,
    [sided.ydomain, [for (series in sided.series, point in series.points)
        [point.errors.plus_y, point.errors.minus_y]]]);
summary(area);
[for (tag in tags(area_html, "tikz-area")) path_data(tag)];
[len(split(area_html, "tikz-legend-entry")) - 1, contains(area_html, "background:blue")];
summary(between);
[for (tag in tags(between_html, "tikz-fill-between")) path_data(tag)];
[failure("\\begin{axis}[ybar, xbar]\\addplot coordinates {(1,1)};\\end{axis}"),
    failure("\\begin{semilogyaxis}[ybar]\\addplot coordinates {(1,1)};\\end{semilogyaxis}"),
    failure("\\begin{axis}\\addplot+[error bars/.cd, y dir=both] coordinates {(0,1)};\\end{axis}"),
    failure("\\begin{axis}\\addplot+[error bars/.cd, y dir=sideways, y explicit]" ++
        " coordinates {(0,1)};\\end{axis}"),
    failure("\\begin{axis}\\addplot+[y dir=both, error bars/.cd, y explicit]" ++
        " coordinates {(0,1)};\\end{axis}"),
    failure("\\begin{axis}\\addplot+[error bars/.cd, y dir=both, y explicit, error mark=o]" ++
        " coordinates {(0,1) +- (0,1)};\\end{axis}"),
    failure("\\begin{axis}\\addplot+[error bars/.cd, z dir=both] coordinates {(0,1)};\\end{axis}"),
    failure("\\begin{axis}\\addplot[name path=A] coordinates {(0,0) (1,1)};" ++
        "\\addplot fill between[of=A and Z];\\end{axis}"),
    failure("\\begin{axis}\\addplot[name path=A] coordinates {(0,0) (1,1)};" ++
        "\\addplot fill between[of=A];\\end{axis}"),
    failure("\\begin{axis}\\addplot fill between[of=A and B];\\end{axis}"),
    failure("\\begin{axis}\\addplot[name path=A] coordinates {(0,0) (1,1)};" ++
        "\\addplot[name path=B] coordinates {(0,1) (1,2)};" ++
        "\\addplot fill between[of=A and B, soft clip={x=1}];\\end{axis}"),
    failure("\\begin{axis}\\addplot coordinates {(0,0) (1,1)} \\closedcycle\\end{axis}"),
    failure("\\begin{axis}[ybar]\\addplot+[error bars/.cd, y dir=both, y explicit]" ++
        " coordinates {(0,1) +- (1,2,3)};\\end{axis}")]
