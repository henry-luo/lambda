// semantic HTML construction is shared by chart and UI skins without a skin dependency.
pub fn heading(attributes, children, level) {
    if (level == 1) <h1 *:attributes,*children>
    else if (level == 2) <h2 *:attributes,*children>
    else if (level == 3) <h3 *:attributes,*children>
    else if (level == 4) <h4 *:attributes,*children>
    else if (level == 5) <h5 *:attributes,*children>
    else <h6 *:attributes,*children>
}
