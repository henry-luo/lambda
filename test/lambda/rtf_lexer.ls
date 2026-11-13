import lexer: lambda.doc.rtf.lexer
import grammar_lexer: lambda.doc.rtf.grammar_lexer
fn tokens(reader, stream, at = 0) any^ {
    let token = reader(stream, at);
    if (token == null) [] else [token, *tokens(reader, stream, token.end)]
}
let sample = "{\\rtf1\\b Bold \\u-1234? \\'e9 \\{x\\}}"
let edge = "\\fs2147483647999 \\li-2147483647999 \\u-? \\'xZ \\~ \\";
tokens(lexer.next,sample)^
tokens(lexer.next,edge)^;
[for (source in [sample,edge,"{\\rtf1 é 漢 \\u233?}","{\\rtf1 {unterminated"])
    tokens(lexer.next,source)^ == tokens(grammar_lexer.next,grammar_lexer.prepare(source)^)^]
