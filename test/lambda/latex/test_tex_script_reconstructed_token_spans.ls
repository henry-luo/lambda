import profile: lambda.doc.latex.tex_engine.profile
import rebuild: lambda.doc.latex.tex_engine.reconstruct
import utf8: lambda.io.utf8
let kernel = input("lmd/package/doc/latex/tex_engine/kernel.tex", "text")^;
let options = {echo_definitions:true, adapters:[], resources:{local:{}, bundled:{}}};

fn expand(source) any^ => profile.expand_with_kernel(source, kernel, options, 100000)
fn emitted_text(built, start, end) any^ => utf8.decode(slice(binary(built.text)^, start, end)^)

// each coalesced record owns all token indices in its range, with no overlaps.
fn covered(spans, limit, index = 0, next = 0, previous_end = 0) any^ {
    if (index == len(spans)) next == limit
    else {
        let s = spans[index];
        s.index == next and s.count > 0 and s.start >= previous_end and s.end >= s.start and
            covered(spans, limit, index + 1, next + s.count, s.end)^
    }
}

fn check(source) any^ {
    let expanded = expand(source)^;
    let normal = rebuild.reconstruct(expanded)^;
    let marked = rebuild.reconstruct(expanded,true)^;
    let native = parse(source,{type:"latex"})^;
    let expected = native[1][0][0].argument_groups[0];
    let opening = [for (span in marked.token_spans where span.count == 1 and
        expanded.output[span.index].value == "(") span];
    let closing = [for (span in marked.token_spans where span.count == 1 and
        expanded.output[span.index].value == ")") span];
    let found = len(opening) == 1 and len(closing) == 1;
    {issue:marked.issue, default_unchanged:normal.token_spans == null and normal.text == marked.text and
        normal.segments == marked.segments and normal.definition_spans == marked.definition_spans,
     covered:covered(marked.token_spans,len(expanded.output))^,
     coalesced:any([for (span in marked.token_spans) span.count > 1]),
     argument:found and emitted_text(marked,opening[0].start + 1,closing[0].end - 1)^ == expected.raw and
        rebuild.map_offset(marked.segments,opening[0].start)^ == expected.start and
        rebuild.map_offset(marked.segments,closing[0].end)^ == expected.end}
}

[for (source in [
    "\\def\\arg{(1,2)}\\begin{document}\\mystery\\arg\\end{document}",
    "\\def\\arg{1,2}\\begin{document}\\mystery(\\arg)\\end{document}",
    "é\\def\\arg{(-1,+2)}\\begin{document}\\mystery\\arg\\end{document}",
    "\\def\\arg{-1,+2}\\begin{document}\\mystery(\\arg)\\end{document}"
]) check(source)^];

let accent = expand("\\begin{document}A\\accent19 e!\\end{document}")^;
let accent_built = rebuild.reconstruct(accent,true)^;
let merged = [for (span in accent_built.token_spans where
    accent.output[span.index].reconstruct_text != null) span];
{accent_issue:accent_built.issue, merged:len(merged) == 1 and merged[0].count > 1,
 spelling:if(len(merged) == 1) emitted_text(accent_built,merged[0].start,merged[0].end)^ else null,
 covered:covered(accent_built.token_spans,len(accent.output))^}

let separator = expand("\\def\\s{\\quad}\\begin{document}\\s A\\end{document}")^;
let separator_built = rebuild.reconstruct(separator,true)^;
let quad = [for (span in separator_built.token_spans where separator.output[span.index].value == "quad") span];
let letter = [for (span in separator_built.token_spans where separator.output[span.index].value == "A") span];
{issue:separator_built.issue, found:len(quad) == 1 and len(letter) == 1,
 separator:if(len(quad) == 1 and len(letter) == 1)
    emitted_text(separator_built,quad[0].end,letter[0].start)^ else null,
 covered:covered(separator_built.token_spans,len(separator.output))^}
