import util: ~~.util

pub fn options(el) => util.parse_kv_options(el.options_raw)

fn label_type(label) {
    if (label == null) null
    else if (not ends_with(trim(label), "*.")) null
    else if (index_of(label, "\\alph") != null) "lower-alpha"
    else if (index_of(label, "\\Alph") != null) "upper-alpha"
    else if (index_of(label, "\\roman") != null) "lower-roman"
    else if (index_of(label, "\\Roman") != null) "upper-roman"
    else if (index_of(label, "\\arabic") != null) "decimal"
    else null
}

pub fn list_style(opts, kind) {
    let margin = if (opts.leftmargin != null and util.css_dimension(opts.leftmargin) != null)
        "padding-left:" ++ util.css_dimension(opts.leftmargin) ++ ";" else ""
    let label = if (kind == "enumerate") label_type(opts.label) else null
    margin ++ (if (label != null) "list-style-type:" ++ label ++ ";" else "")
}

pub fn start(opts) {
    let value = if (opts.start != null) int(opts.start) else null
    if (value != null and value > 0) value else null
}

pub fn unsupported_keys(opts, kind) {
    let allowed = if (kind == "enumerate") ["label", "start", "resume", "leftmargin"]
        else ["leftmargin"]
    let keys = [for (key, value at opts
                where not any([for (name in allowed) string(key) == name])) string(key)]
    let label_issue = if (kind == "enumerate" and opts.label != null and label_type(opts.label) == null) ["label"] else []
    let start_issue = if (kind == "enumerate" and opts.start != null and start(opts) == null) ["start"] else []
    let resume_issue = if (kind == "enumerate" and opts.resume != null and
        opts.resume != "true" and opts.resume != "false")
        ["resume"] else []
    let margin_issue = if (opts.leftmargin != null and util.css_dimension(opts.leftmargin) == null)
        ["leftmargin"] else []
    keys ++ label_issue ++ start_issue ++ resume_issue ++ margin_issue
}
