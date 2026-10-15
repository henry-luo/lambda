// Shipped source formatters; native callers only route package invocation (D7.1.1/D7.2.4).
import c: .misc.common
import markup: .misc.markup
import records: .misc.records
import mail: .misc.mail
pub let formats = ["asciidoc","man","typst","eml","vcf","ics"]
fn canonical(value) => if (value in ["adoc","asciidoc"]) "asciidoc"
    else if (value in ["troff","man"]) "man"
    else if (value in ["vcard","vcf"]) "vcf"
    else if (value in ["icalendar","ical","ics"]) "ics" else value
fn checked(options,extra = []) map^ {
    // Bind validation so its null success value does not join the returned map.
    let valid_options = if (options != null and not (options is map)) raise c.fail("options must be a map")
    let opts = if (options == null) {} else options
    let unknown = [for (key,item in opts where not (string(key) in ["type","flavor","loss","title","section","date","source","manual"] ++ extra)) key]
    let target = canonical(string(c.get(opts,"type",c.get(opts,"flavor","asciidoc"))))
    let loss = c.get(opts,"loss","error");
    if (len(unknown) > 0) raise c.fail("unknown option " ++ string(unknown[0]))
    else if (not (target in formats)) raise c.fail("unsupported target " ++ string(target))
    else if (not (loss in ["error","allow"])) raise c.fail("loss must be error or allow")
    else if (opts.flavor != null and string(opts.flavor) != "" and string(opts.flavor) != "standard" and canonical(string(opts.flavor)) != target)
        raise c.fail("unsupported flavor " ++ string(opts.flavor))
    else if (target != "man" and any([for (key in ["title","section","date","source","manual"]) opts[key] != null]))
        raise c.fail("title/section/date/source/manual options apply to man output")
    else {*:opts,type:target,loss:loss}
}
pub fn format_report(value,options = null) map^ {
    let opts = checked(options)^;
    if (opts.type in ["asciidoc","man","typst"]) markup.render_document(value,opts)^
    else if (opts.type == "eml") mail.render(value)^
    else records.render(value,opts.type)^
}
pub fn format(value,options = null) string^ {
    let opts = checked(options)^
    let report = format_report(value,opts)^;
    if (opts.loss == "error" and len(report.diagnostics) > 0)
        raise c.fail(report.diagnostics[0].code ++ ": " ++ report.diagnostics[0].message)
    else report.text
}
pub fn format_native(value,target,options = null) string^ =>
    format(value,{*:if (options is map) options else {},type:target})^
pub fn format_file(path,options = null) string^ {
    let opts = checked(options,["input_type","input_flavor"])^
    let value = input(path,{type:c.get(opts,"input_type","auto"),flavor:c.get(opts,"input_flavor","")})^
    let writer_options = map([for (key,item in opts where not (string(key) in ["input_type","input_flavor"])) (key,item)]);
    format(value,writer_options)^
}
pub fn asciidoc(value,options = null) string^ => format_native(value,"asciidoc",options)^
pub fn man(value,options = null) string^ => format_native(value,"man",options)^
pub fn typst(value,options = null) string^ => format_native(value,"typst",options)^
pub fn eml(value,options = null) string^ => format_native(value,"eml",options)^
pub fn vcf(value,options = null) string^ => format_native(value,"vcf",options)^
pub fn ics(value,options = null) string^ => format_native(value,"ics",options)^
