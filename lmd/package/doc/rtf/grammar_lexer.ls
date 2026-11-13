// The grammar recognizes the RTF record stream; semantic lowering maintains group state.
import syntax: .grammar
import search: lambda.io.search
import math

fn number_value(source, at, stop, value = 0) any^ {
    if (at >= stop) value else {
        let d = ord(slice(source,at,at+1)) - ord("0");
        number_value(source,at+1,stop,if (value > (2147483647-d)/10) 2147483647 else value*10+d)
    }
}
fn parameter(raw) any^ {
    if (raw == null) null else {
        let negative = starts_with(raw,"-");
        let value = number_value(raw,if (negative) 1 else 0,len(raw))^;
        if (negative) -value else value
    }
}
fn token(node) any^ {
    let kind = string(name(node));
    if (kind == "Control") {kind:"control",word:node.word,parameter:parameter(node.argument),
        width:1+len(node.word)+(if (node.argument == null) 0 else len(node.argument))+
            (if (node.delimiter == null) 0 else 1)}
    else if (kind == "Hex") {kind:"hex",value:index_of("0123456789abcdef",lower(slice(node.value,0,1)))*16+
        index_of("0123456789abcdef",lower(slice(node.value,1,2))),width:4}
    else if (kind == "Symbol") {kind:"symbol",value:node.value or "",width:1+(if (node.value == null) 0 else 1)}
    else if (kind == "Text") {kind:"text",value:node.value,width:len(node.value)}
    else if (kind == "Open") {kind:"open",width:1}
    else {kind:"close",width:1}
}
// All offsets are character offsets, matching Lambda slice, not grammar byte spans.
pub fn prepare(source) map^ {
    let tree = parse(source,syntax.document)^;
    let items = [for (node in (content(tree)^)) token(node)^];
    let ends = math.cumsum([for (item in items) item.width]);
    {source:source,items:items,ends:ends}
}
pub fn next(stream, at) any^ {
    let index = search.lower_bound(stream.ends,at+1)^;
    if (index >= len(stream.items)) null else {
        let item = stream.items[index];
        let end = stream.ends[index];
        let begin = end-item.width;
        if (item.kind == "control") {kind:item.kind,word:item.word,parameter:item.parameter,end:end}
        else if (item.kind == "text") {kind:item.kind,
            value:if (at > begin) slice(item.value,at-begin,len(item.value)) else item.value,end:end}
        else if (item.kind == "hex" or item.kind == "symbol") {kind:item.kind,value:item.value,end:end}
        else {kind:item.kind,end:end}
    }
}
