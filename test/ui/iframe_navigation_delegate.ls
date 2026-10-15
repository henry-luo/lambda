import dom
view <delegation_probe> {
    <html <head <style "iframe{width:400px;height:200px}">> <body
        <iframe id:"frame",["data-navigation-event"]:"chosen",
            srcdoc:"<html><body><a href='chapter.xhtml#note'>Chapter link</a></body></html>">
        <p id:"chosen", "Waiting">>>
}
on chosen(evt) {
    dom.set_text_content(dom.query_selector(dom.document_element(evt.target),"#chosen"),
        dom.get_attribute(evt.target,"data-navigation-href"))
    return 'handled'
}
apply(<delegation_probe>)
