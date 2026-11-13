import rtf: lambda.doc.rtf
import model: lambda.editor.format.model

fn words(source) any^ => model.plain_text(rtf.parse(source)^)^;
[
  words("{\\rtf1\\ansi \\u-10179?\\u-8704?}")^ == "😀",
  model.plain_text(rtf.parse_grammar("{\\rtf1\\ansi \\u-10179?\\u-8704?}")^) == "😀",
  words("{\\rtf1\\ansi \\uc0\\u-10179\\u-8704}")^ == "😀",
  words("{\\rtf1\\ansi A\\u233?B}")^ == "AéB",
  words("{\\rtf1\\ansi \\u-10179?}")^ == "",
  words("{\\rtf1\\ansi \\u-8704?}")^ == "",
  words("{\\rtf1\\ansi \\u-10179?x}")^ == "x"
]
