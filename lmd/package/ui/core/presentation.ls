// semantic HTML construction is shared by chart and UI skins without a skin dependency.
pub fn heading(attributes, children, level) {
    if (level == 1) <h1 *:attributes,*children>
    else if (level == 2) <h2 *:attributes,*children>
    else if (level == 3) <h3 *:attributes,*children>
    else if (level == 4) <h4 *:attributes,*children>
    else if (level == 5) <h5 *:attributes,*children>
    else <h6 *:attributes,*children>
}

// skins share the document shell; each caller validates options and supplies its scoped root.
pub fn page(children, options, family, variables, css, root_class) element {
    <html lang:if ("locale" at options) options.locale else "en-US",
        dir:if ("direction" at options) (if (options.direction == null) "" else string(options.direction)) else "ltr",
        <head <meta charset:"UTF-8"> <title if ("title" at options) options.title else "Lambda UI · " ++ family> <style css>>
        <body style:"margin:0;",<div class:root_class,style:variables,*children>>>
}
