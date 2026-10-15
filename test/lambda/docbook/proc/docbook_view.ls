import viewer: lambda.doc.docbook.view
pn main() {
    let document = viewer.render_document_file("test/input/docbook/showcase.xml")^
    let html = format(document,"html")^
    let illustrated = viewer.render_document_file("test/input/docbook/media.xml")^
    let image_html = format(illustrated,"html")^;
    return [contains(html,"The Riverside Notebook"),contains(html,"href=\"#second-walk\""),
        contains(html,"/package/doc/docbook/resources/xsltng/css/docbook.css"),
        contains(image_html,"src=\"data:image/svg+xml;base64,"),
        not contains(image_html,"src=\"media/")]
}
