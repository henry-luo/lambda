// RAD07-L3: generated math keeps fonts after authored HTML is sanitized.
import lambda.edit.view
fn shown(items) => format(items, 'html')^
fn md_block(src) => [for (c in content([for (c in content(parse(src, 'markdown')^) where c is element and name(c) == 'body') c][0]) where c is element) c][0]
// Authored styles and a lookalike slot cannot replace the generated font resource.
let guarded = shown(markdown_view(md_block("$x$<span data-edit-math-slot=\"0\">authored</span><style>p{color:red}</style>")))
"math projection isolation:";
[contains(guarded, "@font-face"), not contains(guarded, "p{color:red}"),
 contains(guarded, "data-edit-math-slot=\"0\">authored</span>")]
