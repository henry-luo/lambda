import reader: lambda.doc.latex.tex_engine.reader
import stream: lambda.doc.latex.tex_engine.stream
import expansion: lambda.doc.latex.tex_engine.expansion

fn fixture(name) any^ {
    let source = input("test/latex/fixtures/tex_engine/" ++ name ++ ".tex", "text")^;
    let report = expansion.expand(stream.from_reader(reader.reader(source)), 1000000)^;
    if (report.issue != null) ["ERROR: " ++ report.issue]
    else ["== " ++ name ++ " ==", *[for (line in report.messages where ((starts_with(line, "LT:")) ^ { false })) line]]
}

fn pdf_fixture() any^ {
    let source = input("test/latex/fixtures/tex_engine/pdfstrings.tex", "text")^;
    let report = expansion.expand(stream.from_reader(reader.reader(source)), 1000000, {resources: {local: {}}})^;
    if (report.issue != null) ["ERROR: " ++ report.issue]
    else ["== pdfstrings ==", *[for (line in report.messages where ((starts_with(line, "LT:")) ^ { false })) line]]
}

join([*[for (name in ["expansion", "etex", "tokens", "conditionals", "registers"], line in fixture(name)^) line],
    *pdf_fixture()^], "\n")
