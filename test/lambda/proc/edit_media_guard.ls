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
  // D10.2.6v2 admits valid UTF-8 regardless of suffix or signature and preserves NUL.
  let text_result = editor.open_state(text_path, {source: true})^
  let media_result = editor.open_document(media_path, {source: true})^
  let invalid_result = editor.open_document(invalid_path, {source: true}) ^ { ^.message }
  let disguised_result = editor.open_document(disguised_path, {source: true})^
  let pnm_result = editor.open_document(pnm_path, {source: true})^
  let gif_result = editor.open_document(gif_path, {})^
  let pgm_result = editor.open_document(pgm_path, {})^
  let ordinary_result = editor.open_document(ordinary_path, {})^
  print(format([text_result.mode == 'source' and text_result.session.disk_text == "ab\u0000cd",
                media_result is element, contains(invalid_result, "not safely readable as UTF-8 text"),
                disguised_result is element, pnm_result is element, gif_result is element,
                pgm_result is element, ordinary_result is element], 'json')^ ++ "\n")
}
