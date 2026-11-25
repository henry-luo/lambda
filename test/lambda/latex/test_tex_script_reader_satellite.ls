// S11.4.11: a hot typed handler must evaluate its imported predicate in its owner.
import reader: lambda.doc.latex.tex_engine.reader
fn guarded(source) any^ {
    let input = reader.load_line(reader.reader(source)) or reader.reader("");
    reader.verb_line(input) ^reader.CaptureError { "capture" }^
}
let results = [for (i in 1 to 80) guarded(if (i % 2 == 0) "|bad" else "|ok|")^];
len(results) == 80 and all([for (i in 0 to 79)
    if (i % 2 == 1) results[i] == "capture" else results[i].end == 4])
