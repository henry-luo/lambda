// Shared pure formatter helpers; normalized elements stay ordinary values (S2.6, D7.2.4).
pub fn fail(message) error => error({code:7802,message:"doc.misc: " ++ message})
pub fn get(value,key,fallback = null) => if (value[key] == null) fallback else value[key]
pub fn tag(value) => if (value is element) string(name(value)) else ""
pub fn kids(value) => if (value is element) content(value) else []
pub fn times(value,repetitions) => join([for (i in range(0,max(0,repetitions),1)) value],"")
pub fn result(text: string,issues: array = []) map => {text:text,diagnostics:issues}
pub fn issue(code,node,message) => {code:code,severity:"warning",element:tag(node),message:message}
pub fn merge(values,separator = "") map^ => result(join([for (value in values) value.text],separator),
    [for (value in values,diagnostic in value.diagnostics) diagnostic])
pub fn lost(code,node,message,text = "") map^ => result(text,[issue(code,node,message)])
pub fn text(value) => if (value is string or value is symbol) string(value)
    else join([for (child in kids(value)) text(child)],"")
pub fn sequence(value) => if (value == null) [] else if (value is array or value is list) value else [value]
pub fn newline(value) => replace(replace(value,"\r\n","\n"),"\r","\n")
pub fn crlf(value) => replace(newline(value),"\n","\r\n")
pub fn single_line(value,label) string^ {
    let valid_type = if (not (value is string or value is symbol or value is int)) raise fail(label ++ " must be text")
    let value_text = string(value);
    if (any([for (ch in split(value_text,"")) (ord(ch) < 32 and ord(ch) != 9) or ord(ch) == 127]))
        raise fail(label ++ " contains a control character")
    else value_text
}
pub fn token(value,label,extra = "") string^ {
    let value_text = single_line(value,label)^
    let allowed = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-" ++ extra;
    if (value_text == "" or any([for (ch in split(value_text,"")) not contains(allowed,ch)]))
        raise fail("invalid " ++ label)
    else value_text
}
pub fn text_escape(value) => replace(replace(replace(replace(newline(string(value)),"\\","\\\\"),"\n","\\n"),";","\\;"),",","\\,")
pub fn unknown(value,keys,label) => if (not (value is map or value is element)) [] else
    // Element iteration visits children; attribute checks need the map facet.
    [for (key,item in if (value is element) map(value) else value where not (string(key) in keys) and item != null)
        issue("unsupported-field",value,label ++ ": " ++ string(key))]
pub fn byte_count(value) => len(binary(value))
// Reserve room for a complete UTF-8 scalar and the continuation space.
pub fn fold_line(value,width = 75) {
    let chars = split(value,"")
    let sizes = [for (ch in chars) byte_count(ch)]
    let ends = if (len(chars) == 0) [] else math.cumsum(sizes)
    let offsets = [for (i,ch in chars) ends[i]-sizes[i]]
    let step = max(4,width-4);
    join([for (i,ch in chars) if (i > 0 and floor(offsets[i]/step) != floor(offsets[i-1]/step)) "\r\n " ++ ch else ch],"") ++ "\r\n"
}
pub fn base64(value) {
    let bytes = binary(value)
    let alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    join([for (i in range(0,len(bytes),3)) {
        let a = int(bytes[i])
        let b = if (i+1 < len(bytes)) int(bytes[i+1]) else 0
        let d = if (i+2 < len(bytes)) int(bytes[i+2]) else 0;
        slice(alphabet,int(floor(a/4)),int(floor(a/4))+1) ++
        slice(alphabet,(a%4)*16+int(floor(b/16)),(a%4)*16+int(floor(b/16))+1) ++
        (if (i+1 < len(bytes)) slice(alphabet,(b%16)*4+int(floor(d/64)),(b%16)*4+int(floor(d/64))+1) else "=") ++
        (if (i+2 < len(bytes)) slice(alphabet,d%64,d%64+1) else "=")
    }],"")
}
pub fn base64_lines(value) {
    let encoded = base64(value);
    join([for (i in range(0,len(encoded),76)) slice(encoded,i,i+76)],"\r\n")
}
