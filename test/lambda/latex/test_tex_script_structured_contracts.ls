import expansion: lambda.doc.latex.tex_engine.expansion
import profile: lambda.doc.latex.tex_engine.profile
import reader: lambda.doc.latex.tex_engine.reader
import stream: lambda.doc.latex.tex_engine.stream
import dimension: lambda.doc.latex.tex_engine.dimension
import glue: lambda.doc.latex.tex_engine.glue
import fp: lambda.doc.latex.tex_engine.fp
import pdf: lambda.doc.latex.tex_engine.pdfstrings
import md5: lambda.doc.latex.tex_engine.md5

let input = stream.from_reader({*:reader.reader("{a}"), catcodes:{'{':1,'}':2}});
let group = stream.argument(input)^;
let missing = stream.argument(stream.from_reader(reader.reader("")))^;
[group.error == null, len(group.tokens) == 1, group.tokens[0].value == "a",
 missing.error == "tex-runaway-argument", missing.cause == "missing",
 input.reader.position == 0, (stream.next(group.stream)^).token.value == " "];
let optional = stream.optional_argument(stream.from_reader(reader.reader("x")))^;
[optional.present == false, len(optional.tokens) == 0,
 (stream.next(optional.stream)^).token.value == "x"];
[dimension.unit("pt") == [1,1], dimension.unit("unknown") == null,
 dimension.format(65536) == "1.0pt", dimension.round_decimal([5]) == 32768,
 (glue.add(glue.zero(), {width:65536,stretch:2,stretch_order:1,shrink:0,shrink_order:0})^).width == 65536];
[(fp.evaluate("1+2*3")^).value == "7", (fp.evaluate("1/0")^).issue == "expl3-fp-division-by-zero",
 (fp.evaluate("sqrt(-1)")^).issue == "expl3-fp-domain", (fp.evaluate("1e999")^).issue == "expl3-fp-range",
 (fp.evaluate("1e")^).issue == "expl3-fp-number"];
[(pdf.escape_hex("é")^) == "C3A9", (pdf.unescape_hex("4x1 4")^) == "A@",
 (md5.digest("")^) == "D41D8CD98F00B204E9800998ECF8427E",
 (md5.digest("abc")^) == "900150983CD24FB0D6963F7D28E17F72"];

[(md5.digest(binary("abc")^)^) == "900150983CD24FB0D6963F7D28E17F72",
 (pdf.escape_hex(binary("é")^)^) == "C3A9"];
[(expansion.expand(stream.from_reader(reader.reader("")),10,42)^).issue == null,
 (profile.expand_with_kernel("","",42,10)^).issue == null]
