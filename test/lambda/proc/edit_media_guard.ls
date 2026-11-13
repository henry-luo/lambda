import editor: lambda.editor.app

pn main() {
  let text_path = "temp/edit-media-nul.txt"
  let media_path = "temp/edit-media.pdf"
  let invalid_path = "temp/edit-invalid-utf8.txt"
  let disguised_path = "temp/edit-disguised-pdf.txt"
  output(b'\x6162006364', text_path)^
  output(b'\x25504446', media_path)^
  output(b'\x61628063', invalid_path)^
  output(b'\x255044462D312E340A', disguised_path)^
  let text_result = editor.open_document(text_path, {source: true}) ^ { ^.message }
  let media_result = editor.open_document(media_path, {source: true}) ^ { ^.message }
  let invalid_result = editor.open_document(invalid_path, {source: true}) ^ { ^.message }
  let disguised_result = editor.open_document(disguised_path, {source: true}) ^ { ^.message }
  print(format([contains(text_result, "not safely readable as text"),
                contains(media_result, "non-text document"),
                contains(invalid_result, "not safely readable as text"),
                contains(disguised_result, "non-text document")], 'json')^ ++ "\n")
}
