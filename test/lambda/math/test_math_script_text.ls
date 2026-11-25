// D10.2.2v2: Script math retains text structure, embedded math and tags.
import math: lambda.doc.math.math
import svg: .mod_svg_snapshot
fn paint(source) any^ => svg.painted_text(math.render_box(parse(source,'math')^)^.element)
let tagged = parse("\\tag{Value}\\tag*{Raw}", 'math')^;
let escaped = parse("\\text{a\\%b}", 'math')^;
let raised = parse("\\raisebox{1em}[2ex][3pt]{x}", 'math')^;
[
    paint("\\text{for $a<b$ and $c<d$}") == "for a<b and c<d",
    paint("\\textsf{a \\textbf{b} \\textit{c}}") == "a b c",
    paint("\\text{c~ {ab} \\ e}") == "c  ab  e",
    paint("\\text{a\\it b\\rm c}") == "abc",
    paint("\\text{\\'a}") == "á",
    name(tagged[0]) == 'equation_tag' and tagged[0].starred == false and tagged[0].body.source == "Value",
    name(tagged[1]) == 'equation_tag' and tagged[1].starred == true and tagged[1].body.source == "Raw",
    escaped[0].content == "a%b" and escaped[0].content_raw == "a\\%b",
    raised[0].raise == "1em" and raised[0].height == "2ex" and raised[0].depth == "3pt"
]
