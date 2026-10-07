// D4.5.1v4: evaluator ownership keeps native storage alive through weak wrappers.
import dom
import radiant

pn retained_declaration() {
    let document = dom.load("test/lambda/dom/stylesheet.html")
    let sheet = dom.stylesheets(document)[0]
    let declaration = dom.rule_get_style(dom.stylesheet_rule_at(sheet, 0))
    dom.rule_style_set_value(declaration, "width", "13px", "important")
    declaration
}

pn main() {
    let retained = retained_declaration()
    let other = dom.load("test/lambda/dom/stylesheet.html")
    let sheet = dom.stylesheets(other)[0]
    let declaration = dom.rule_get_style(dom.stylesheet_rule_at(sheet, 0))
    radiant.free(other)
    print([dom.rule_style_get_value(retained, "width"),
        dom.rule_style_get_priority(retained, "width"),
        dom.rule_style_get_value(declaration, "color") == ""])
    print("\n")

    let root = dom.load("test/lambda/dom/stylesheet.html")
    let root_sheet = dom.stylesheets(root)[0]
    let root_rule = dom.stylesheet_rule_at(root_sheet, 0)
    print(dom.rule_style_get_value(dom.rule_get_style(root_rule), "color"))
    print("\n")
    radiant.free(root)
    print(dom.rule_style_get_value(retained, "width"))
    print("\n")
}
