import c: lambda.ui.core.component

pub fn descriptor(kind, props, child) element^ {
    let flags = c.boolean_props(props,["strong","italic","underline","delete","code","mark","keyboard","disabled"],kind)^;
    if (kind == 'title' and props.level != null and (not (props.level is int) or props.level < 1 or props.level > 5))
        raise c.fail(kind,"level must be an int from 1 to 5")
    else if (not c.enum_valid(props.type,["secondary","success","warning","danger"])^) raise c.fail(kind,"invalid text type")
    else if (kind == 'link' and (not all([for (key in ["href","target","rel"]) props[key] == null or props[key] is string]) or
        (props.download != null and not (props.download is string or props.download is bool)))) raise c.fail(kind,"link attributes must be strings; download may also be bool")
    else c.node(kind,props,child,["strong","italic","underline","delete","code","mark","keyboard","disabled","type",
        *(if (kind == 'title') ["level"] else if (kind == 'link') ["href","target","rel","download"] else [])])^
}
fn decorated(node) {
    let p = node.props
    let children = c.contents(node)
    let strong = if (p.strong) [<strong *children>] else children
    let italic = if (p.italic) [<em *strong>] else strong
    let underline = if (p.underline) [<u *italic>] else italic
    let deleted = if (p.delete) [<del *underline>] else underline
    let code = if (p.code) [<code *deleted>] else deleted
    let marked = if (p.mark) [<mark *code>] else code;
    if (p.keyboard) [<kbd *marked>] else marked
}
view dtna_typography: <dtna kind:'title' | 'text' | 'paragraph' | 'link'> {
    let p = ~.props
    let level = c.option(p,"level",1)
    let attributes = {*:c.styled(~,"dtna-typography" ++ (if (p.type == null) "" else " dtna-text-" ++ c.text(p.type)) ++
        (if (p.disabled) " dtna-text-disabled" else "")),*:(if (p.disabled) {['aria-disabled']:"true"} else {})}
    let children = decorated(~);
    if (~.kind == 'title') c.heading({*:attributes,style:c.style_with(p,"font-size:var(--dtna-font-size-heading-" ++ string(level) ++ ");line-height:var(--dtna-font-height-heading-" ++ string(level) ++ ");")},children,level)
    else if (~.kind == 'text') <span *:attributes,*children>
    else if (~.kind == 'paragraph') <p *:attributes,*children>
    // null is a present HTML attribute; omit href entirely for a disabled link.
    else <a *:attributes,*:map([for (key in ["href","target","rel"] where p[key] != null and (key != "href" or not p.disabled)) (key,p[key])]),
        tabindex:if (p.disabled) "-1" else p.tabindex,
        *:(if (p.rel == null and p.target == "_blank") {rel:"noopener noreferrer"} else {}),
        *:(if (p.download == true) {download:""} else if (p.download is string) {download:p.download} else {}),*children>
}
on click(evt) { if (~.props.disabled) 'prevent-default' else 'pass' }
pub let css = "
.dtna-typography{color:inherit}.dtna-text-secondary{color:var(--dtna-text-secondary)}.dtna-text-success{color:var(--dtna-success)}
.dtna-text-warning{color:var(--dtna-warning)}.dtna-text-danger{color:var(--dtna-error)}.dtna-text-disabled{color:var(--dtna-disabled-text);cursor:default;user-select:none}
.dtna-typography code{font-family:monospace;font-size:85%;border:1px solid var(--dtna-border);border-radius:3px;padding:0.2em 0.4em;background:var(--dtna-surface)}
.dtna-typography kbd{font-family:monospace;font-size:90%;border:1px solid var(--dtna-border);border-bottom-width:2px;border-radius:3px;padding:0.15em 0.4em;background:var(--dtna-surface)}
.dtna-typography mark{padding:0;background:#ffe58f}.dtna-typography strong{font-weight:600}.dtna-link{color:var(--dtna-primary)}.dtna-link.dtna-text-disabled{color:var(--dtna-disabled-text)}
.dtna-typography + .dtna-title{margin-top:1.2em}.dtna-link.dtna-text-disabled:hover{text-decoration:none}
"
