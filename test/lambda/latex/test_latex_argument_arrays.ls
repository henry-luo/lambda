// S2.6.1v2/S2.6.4: command arrays preserve argument positions while element text merges.
import util: lambda.latex.util
import analyze: lambda.latex.analyze
import latex: lambda.latex.latex

let ast = parse("\\definecolor{accent}{HTML}{FF8000}\\setcounter{secnumdepth}{2}\\newtheorem{claim}{Claim}", 'latex')^;
let color = util.find_descendant(ast, 'definecolor');
[len(color), color[0] is array, color[0]];
util.command_args(color)
let info = analyze.analyze(ast);
[util.lookup(info.custom_colors, "accent"), info.secnumdepth, util.lookup(info.theorem_defs, "claim").label];

let mixed_ast = parse("\\cmd{a}{b}[opt]{c}{d}{}", 'latex')^;
let mixed = util.find_descendant(mixed_ast, 'cmd');
[len(mixed), mixed[0], name(mixed[1]), mixed[2], name(mixed[3])];
util.command_args(mixed)

let text_ast = parse("\\textbf{a\\%b\\ c}", 'latex')^;
let text = util.find_descendant(text_ast, 'textbf');
[len(text), text[0]];
util.text_of(<cmd ["a", "b"]>)
util.rich_text_of(<cmd ["a", "b"]>)
format(color, 'latex')

let html = latex.render_string("\\newcommand{\\pair}[2]{#1 / #2}\\pair{left}{right} \\href{https://example.com}{Link} \\textcolor{red}{Colored} \\parbox{2cm}{Box}");
let rendered_text = util.text_of(html);
[contains(rendered_text, "left / right"), contains(rendered_text, "Link"), contains(rendered_text, "Colored"), contains(rendered_text, "Box"), contains(rendered_text, "2cm")];

let table = latex.render_string_to_html("\\begin{tabular}{ll}\\multicolumn{2}{c}{Span} \\\\ \\multirow{2}{*}{Rows}&cell\\end{tabular}");
[contains(table, "colspan=\"2\""), contains(table, "rowspan=\"2\""), contains(table, "Span"), contains(table, "Rows")]
