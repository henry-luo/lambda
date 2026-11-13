import lexer: lambda.doc.rtf.lexer
fn tokens(source, at = 0) any^ {
    let token = lexer.next(source, at);
    if (token == null) [] else [token, *tokens(source, token.end)]
}
tokens("{\\rtf1\\b Bold \\u-1234? \\'e9 \\{x\\}}")^
tokens("\\fs2147483647999 \\li-2147483647999 \\u-? \\'xZ \\~ \\")^
