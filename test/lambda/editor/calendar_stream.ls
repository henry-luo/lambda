import rfc: lambda.editor.format.rfc
import misc: lambda.doc.misc

let text = "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nBEGIN:VEVENT\r\nUID:first\r\nDTSTAMP:20260101T000000Z\r\nDTSTART:20260101T000000Z\r\nSUMMARY:First\r\nEND:VEVENT\r\nEND:VCALENDAR\r\nBEGIN:VCALENDAR\r\nVERSION:2.0\r\nBEGIN:VEVENT\r\nUID:second\r\nDTSTAMP:20260101T000000Z\r\nDTSTART:20260101T000000Z\r\nSUMMARY:Second\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n"
let loaded = rfc.import_text(text)^
let second = loaded.doc.tree.children[1].node.children[1].node.children[3].node
let edited = rfc.replace_scalar(loaded.doc, second.first, second.last, "Changed")^
let parsed = parse(edited.doc.text, 'ics')^
let serialized = misc.ics(parsed)^
let reparsed = parse(serialized, 'ics')^;
[
  len(loaded.doc.tree.children) == 2,
  contains(edited.doc.text, "SUMMARY:First\r\nEND:VEVENT\r\nEND:VCALENDAR\r\nBEGIN:VCALENDAR"),
  contains(edited.doc.text, "SUMMARY:Changed\r\n"),
  len(parsed) == 2 and parsed[0].components[0].summary == "First" and parsed[1].components[0].summary == "Changed",
  len(reparsed) == 2 and reparsed[0].components[0].summary == "First" and reparsed[1].components[0].summary == "Changed",
  (rfc.import_text("BEGIN:VCALENDAR\nEND:VCALENDAR\nBEGIN:VCALENDAR\nBEGIN:VEVENT\nEND:VCALENDAR\n") ^ { null }) == null
]
