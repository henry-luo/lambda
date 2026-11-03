import d: lambda.doc.docx
fn code(options) int {
    let converted = d.convert("test/input/docx/minimal.docx", options);
    if (converted is error) converted.code else -1
}
let empty = d.convert("test/input/docx/empty.docx")^
let preserved = d.convert("test/input/docx/empty.docx",{include_empty_paragraphs:true})^;
[
    empty.html == "<article class=\"docx-document\"></article>",
    preserved.html == "<article class=\"docx-document\"><p></p><p></p></article>",
    preserved.text == "\n\n\n\n",
    code({unknown:true})^ == 7601,
    code({style_map:{Normal:"script"}})^ == 7601,
    code({style_map:{Normal:{tag:"p",onclick:"bad"}}})^ == 7601,
    code({style_map:[]})^ == 7601,
    code({images:"file"})^ == 7601,
    code({include_empty_paragraphs:1})^ == 7601,
    code({unsupported:"silent"})^ == 7601,
    code({id_prefix:"unsafe\n"})^ == 7601
]
