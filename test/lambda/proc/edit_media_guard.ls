import editor: lambda.editor.app

pn main() {
  let text_path = "temp/edit-media-nul.txt"
  let media_path = "temp/edit-media.pdf"
  let invalid_path = "temp/edit-invalid-utf8.txt"
  let disguised_path = "temp/edit-disguised-pdf.txt"
  let pnm_path = "temp/edit-disguised-image.txt"
  let gif_path = "temp/edit-disguised-gif.txt"
  let pgm_path = "temp/edit-disguised-pgm"
  let ordinary_path = "temp/edit-text-p3.txt"
  output(b'\x6162006364', text_path)^
  output(b'\x25504446', media_path)^
  output(b'\x61628063', invalid_path)^
  output(b'\x255044462D312E340A', disguised_path)^
  output("P3\n1 1\n255\n255 0 0\n", pnm_path)^
  output("GIF89aASCII", gif_path)^
  output("P2\n# comment\n1 1\n255\n0\n", pgm_path)^
  output("P3 is a document section\n", ordinary_path)^
  let text_result = editor.open_document(text_path, {source: true}) ^ { ^.message }
  let media_result = editor.open_document(media_path, {source: true}) ^ { ^.message }
  let invalid_result = editor.open_document(invalid_path, {source: true}) ^ { ^.message }
  let disguised_result = editor.open_document(disguised_path, {source: true}) ^ { ^.message }
  let pnm_result = editor.open_document(pnm_path, {source: true}) ^ { ^.message }
  let gif_result = editor.open_document(gif_path, {}) ^ { ^.message }
  let pgm_result = editor.open_document(pgm_path, {}) ^ { ^.message }
  let ordinary_result = editor.open_document(ordinary_path, {}) ^ { null }
  print(format([contains(text_result, "not safely readable as text"),
                contains(media_result, "non-text document"),
                contains(invalid_result, "not safely readable as text"),
                contains(disguised_result, "non-text document"),
                contains(pnm_result, "non-text document"),
                contains(gif_result, "non-text document"),
                contains(pgm_result, "non-text document"), ordinary_result is element], 'json')^ ++ "\n")
}
