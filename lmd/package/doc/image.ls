// D7.2.4v3: image composition belongs to lambda.image; this adapter presents its pixels.
import img: lambda.image
import media: .media

pub fn preview(document) element^ {
    let source = media.data_uri(img.encode(document, {format: 'png'})^, "image/png")^;
    <img class:"rdt-image-document", src:source, width:document.width, height:document.height,
        alt:"Image preview">
}

pub pn render_document_file(source, options = null) element^ {
    let document = img.read(source)^;
    <html
        <head <meta charset:"UTF-8"> <title "Image preview">
            <style "html,body{margin:0;padding:0;min-width:100%;min-height:100%;background:#fff}body{overflow:auto}img.rdt-image-document{display:block;max-width:none;height:auto;user-select:auto}">>
        <body 'data-rdt-document':"image", preview(document)^>>
}
