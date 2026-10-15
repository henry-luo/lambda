import util: ~~.util

pub fn options(el) => util.parse_kv_options(el.options_raw)

fn label_type(label) {
    if (label == null) null
    else if (not ends_with(trim(label), "*.") and
             not (ends_with(trim(label), "*)") and
                  index_of(label, "\\alph") != null)) null
    else if (index_of(label, "\\alph") != null) "lower-alpha"
    else if (index_of(label, "\\Alph") != null) "upper-alpha"
    else if (index_of(label, "\\roman") != null) "lower-roman"
    else if (index_of(label, "\\Roman") != null) "upper-roman"
    else if (index_of(label, "\\arabic") != null) "decimal"
    else null
}

pub fn parenthesized_alpha(opts) {
    if (opts.label == null) false
    else ends_with(trim(opts.label), "*)") and
        index_of(opts.label, "\\alph") != null
}

pub fn list_style(opts, kind) {
    let margin = if (opts.leftmargin != null and util.css_dimension(opts.leftmargin) != null)
        "padding-left:" ++ util.css_dimension(opts.leftmargin) ++ ";" else ""
    let label = if (kind == "enumerate") label_type(opts.label) else null
    let compact = util.option_enabled(opts.nosep)
    let top = if (compact) "margin-top:0;margin-bottom:0;"
        else if (opts.topsep != null) "margin-top:" ++ util.css_dimension(opts.topsep) ++
            ";margin-bottom:" ++ util.css_dimension(opts.topsep) ++ ";" else ""
    let item = if (compact) "--latex-itemsep:0;--latex-parsep:0;"
        else (if (opts.itemsep != null) "--latex-itemsep:" ++ util.css_dimension(opts.itemsep) ++ ";" else "") ++
             (if (opts.parsep != null) "--latex-parsep:" ++ util.css_dimension(opts.parsep) ++ ";" else "")
    margin ++ top ++ item ++ (if (label != null) "list-style-type:" ++ label ++ ";" else "")
}

pub fn start(opts) {
    let value = if (opts.start != null) int(opts.start) ^ { null } else null
    if (value != null and value > 0) value else null
}

pub fn unsupported_keys(opts, kind) {
    let spacing = ["leftmargin", "topsep", "itemsep", "parsep", "nosep"]
    let allowed = if (kind == "enumerate") spacing ++ ["label", "start", "resume"]
        else spacing
    let keys = [for (key, value at opts
                where not any([for (name in allowed) string(key) == name])) string(key)]
    let label_issue = if (kind == "enumerate" and opts.label != null and label_type(opts.label) == null) ["label"] else []
    let start_issue = if (kind == "enumerate" and opts.start != null and start(opts) == null) ["start"] else []
    let resume_issue = if (kind == "enumerate" and opts.resume != null and
        opts.resume != "true" and opts.resume != "false")
        ["resume"] else []
    let margin_issue = if (opts.leftmargin != null and util.css_dimension(opts.leftmargin) == null)
        ["leftmargin"] else []
    let spacing_issues = [for (key in ["topsep", "itemsep", "parsep"]
        where opts[key] != null and util.css_dimension(opts[key]) == null) key]
    let nosep_issue = if (opts.nosep != null and opts.nosep != "true" and opts.nosep != "false")
        ["nosep"] else []
    keys ++ label_issue ++ start_issue ++ resume_issue ++ margin_issue ++ spacing_issues ++ nosep_issue
}
