// Wiki formatting is a shipped Lambda package; native IO remains runtime-free (D7.1.1, D7.2.4).
import profiles: .wiki.profiles
import writer: .wiki.writer
import c: .misc.common
fn fail(message) error => error({code:7801,message:"wiki: " ++ message})
fn checked_keys(options,keys) map^ {
    if (options != null and not (options is map)) raise fail("options must be a map")
    let value = c.get({v:options},"v",{})
    let unknown = [for (key,item in value where not (key in keys)) key];
    if (len(unknown) > 0) raise fail("unknown option " ++ string(unknown[0]))
    else value
}
fn checked(options) map^ {
    let value = checked_keys(options,["type","flavor","loss"])^
    let flavor = c.get(value,"flavor","mediawiki")
    let profile = profiles.get(flavor)
    let loss = c.get(value,"loss","error");
    if (profile == null) raise fail("unknown flavor " ++ string(flavor))
    else if (not (loss in ["error","allow"])) raise fail("loss must be error or allow")
    else {profile:profile,loss:loss}
}
pub fn format_report(document,options = null) map^ {
    let opts = checked(options)^;
    writer.render(document,{profile:opts.profile,prefix:"",depth:0})^
}
pub fn format(document,options = null) string^ {
    let report = format_report(document,options)^
    let loss = c.get(options,"loss","error");
    if (loss == "error" and len(report.diagnostics) > 0) raise fail(report.diagnostics[0].code ++ ": " ++ report.diagnostics[0].message)
    else report.text
}
// Runtime passes the original options through without allocating an intermediate Mark tree.
pub fn format_native(document,flavor,options = null) string^ =>
    format(document,{*:if (options is map) options else {},flavor:flavor})^
pub fn format_file(path,options = null) string^ {
    let opts = checked_keys(options,["input_type","input_flavor","flavor","loss"])^
    let document = input(path,{type:c.get(opts,"input_type","auto"),flavor:c.get(opts,"input_flavor","")})^;
    format(document,{flavor:c.get(opts,"flavor","mediawiki"),loss:c.get(opts,"loss","error")})^
}
