import c: .common
import epub: .epub
import media: lambda.doc.media
import uri: lambda.doc.uri

pub fn escape(value) => replace(replace(replace(replace(string(value),"&","&amp;"),"<","&lt;"),">","&gt;"),"\"","&quot;")
pub fn mime(book,path) {
    let entry = c.first([for (entry in book.manifest where entry.href == path) entry]);
    if (entry != null) entry.media_type else c.get({svg:"image/svg+xml",png:"image/png",jpg:"image/jpeg",
        gif:"image/gif",woff:"font/woff",woff2:"font/woff2",ttf:"font/ttf"},split(path,".")[len(split(path,"."))-1],"application/octet-stream")
}
pub fn bytes(book,path) any^ {
    let member = book.resources[path];
    if (member == null) raise c.fail("missing-part","Missing resource " ++ path,path)
    else if (member is map) member.bytes else parse(member,"binary")^
}
pub fn asset(book,owner,value,sanitizer = null,chain = []) map^ {
    if (not (value is string) or value == "" or starts_with(value,"//") or contains(value,":"))
        {value:"",diagnostics:[c.issue("blocked-resource",owner,"External or active resource was blocked")]}
    else {
        let target = c.target(owner,value)^
        let restriction = c.first([for (entry in book.restrictions where entry.href == target.href) entry]);
        if (restriction != null) {value:"",diagnostics:[c.issue("protected-resource",target.href,"Protected resource uses a fallback")]}
        else if (book.resources[target.href] == null)
            {value:"",diagnostics:[c.issue("missing-resource",target.href,"Resource was not found")]}
        else if (target.href in chain or len(chain) >= 16)
            raise c.fail("limit","Resource cycle/depth limit",target.href)
        else if (mime(book,target.href) == "image/svg+xml" and sanitizer != null) {
            let result = sanitizer(book,target.href,[*chain,target.href])^;
            {value:media.data_uri(binary(result.text),"image/svg+xml")^ ++
                (if (target.fragment == null) "" else "#" ++ uri.component(target.fragment)),diagnostics:result.diagnostics}
        }
        else if (mime(book,target.href) == "image/svg+xml")
            {value:"",diagnostics:[c.issue("unsupported-svg-image",target.href,"SVG requires publication-aware sanitization")]}
        else {value:media.data_uri(bytes(book,target.href)^,mime(book,target.href))^ ++
            (if (target.fragment == null) "" else "#" ++ uri.component(target.fragment)),diagnostics:[]}
    }
}
fn closing(tokens,index,terminator) int^ {
    let matches = [for (i,token in tokens where i >= index and token.raw == terminator) i];
    if (len(matches) == 0) raise c.fail("invalid-package","Unterminated CSS URL/import") else matches[0]
}
fn url_value(tokens,index) map^ {
    let end = closing(tokens,index+1,")")^
    let pieces = [for (i,token in tokens where i > index and i < end and token.kind != "space") token];
    {value:if (len(pieces) == 1 and pieces[0].kind == "string") pieces[0].value
        else join([for (token in pieces) token.value],""),next:end+1}
}
fn rewrite_token(book,tokens,index,owner,chain,sanitizer) map^ {
        let token = tokens[index]
        let function_name = lower(token.value)
        let is_url = token.kind == "function" and function_name == "url("
        let is_import = token.kind == "at" and lower(token.value) == "@import"
        if (is_import) {
            let end = closing(tokens,index+1,";")^
            let entries = [for (i,t in tokens where i > index and i < end and t.kind != "space") {index:i,token:t}]
            let first = entries[0]
            let url = if (first.token.kind == "string") {value:first.token.value,next:first.index+1}
                else if (first.token.kind == "url") {value:first.token.value,next:first.index+1}
                else if (first.token.kind == "function" and lower(first.token.value) == "url(") url_value(tokens,first.index)^
                else raise c.fail("invalid-package","Invalid CSS import",owner)
            let target = if (contains(url.value,":") or starts_with(url.value,"//")) null else c.target(owner,url.value)^
            let result = if (target == null) {text:"",diagnostics:[c.issue("blocked-resource",owner,"External stylesheet blocked")]}
                else stylesheet(book,target.href,chain,sanitizer)^
            let condition = trim(join([for (i,t in tokens where i >= url.next and i < end) t.raw],""));
            if (contains(condition,"layer") or contains(condition,"supports"))
                raise c.fail("unsupported","Layer/supports CSS imports are outside the initial profile",owner)
            else {text:if (condition == "") result.text else "@media " ++ condition ++ "{" ++ result.text ++ "}",
                diagnostics:result.diagnostics,next:end+1}
        } else if (is_url or token.kind == "url") {
            let url = if (token.kind == "url") {value:token.value,next:index+1} else url_value(tokens,index)^
            let result = if (starts_with(url.value,"#")) {value:url.value,diagnostics:[]} else asset(book,owner,url.value,sanitizer,chain)^;
            {text:"url(" ++ format(result.value,"json")^ ++ ")",diagnostics:result.diagnostics,next:url.next}
        } else if (token.kind == "function" and function_name in ["image-set(","-webkit-image-set("])
            raise c.fail("unsupported","CSS image-set needs an explicit resource profile",owner)
        else {text:token.raw,diagnostics:[],next:index+1}
}
// Divide the token stream so ordinary large stylesheets use logarithmic call depth.
fn rewrite(book,tokens,start,end,owner,chain,sanitizer) map^ {
    if (start >= end) {text:"",diagnostics:[],next:start}
    else if (end-start == 1) rewrite_token(book,tokens,start,owner,chain,sanitizer)^
    else {
        let middle = start+int(floor((end-start)/2))
        let left = rewrite(book,tokens,start,middle,owner,chain,sanitizer)^
        let right = rewrite(book,tokens,max(middle,left.next),end,owner,chain,sanitizer)^;
        {text:left.text ++ right.text,diagnostics:left.diagnostics ++ right.diagnostics,next:right.next}
    }
}
pub fn css(book,text,owner,chain = [],sanitizer = null) map^ {
    let tokens = parse(text,{type:"css",flavor:"tokens"})^;
    let result = rewrite(book,tokens,0,len(tokens),owner,chain,sanitizer)^;
    // HTML raw-text serialization must preserve the CSS string without admitting an end tag.
    {*:result,text:replace(result.text,"</","<\\/")}

}
pub fn stylesheet(book,path,chain = [],sanitizer = null) map^ {
    if (path in chain or len(chain) >= 16) raise c.fail("limit","CSS import cycle/depth limit",path)
    else css(book,epub.source_text(book,path)^,path,[*chain,path],sanitizer)^
}
