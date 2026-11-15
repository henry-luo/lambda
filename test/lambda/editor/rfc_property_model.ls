import rfc: lambda.editor.format.rfc

let text = "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nBEGIN:VEVENT\r\nSUMMARY;LANGUAGE=en:Folded \r\n value\r\nATTENDEE;CN=\"a:b\":mailto:a@example.test\r\nATTENDEE:mailto:b@example.test\r\nBEGIN:VALARM\r\nDESCRIPTION:Wake\\nup\r\nEND:VALARM\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n"
let loaded = rfc.import_text(text)^
let calendar = loaded.doc.tree.children[0].node
let event = calendar.children[1].node
let summary = event.children[0]
let changed = rfc.replace_scalar(loaded.doc, summary.node.first, summary.node.last, "Folded changed value")^
let renamed = rfc.rename_key(loaded.doc, summary.first, summary.key_end, "DESCRIPTION")^
let added = rfc.insert_child(loaded.doc, event.first, "LOCATION", "Room 2")^
let deleted = rfc.delete_child(loaded.doc, event.children[1].node.first)^
let alarm_deleted = rfc.delete_child(loaded.doc, event.children[3].node.first)^
let cards = rfc.vcf_descriptor.import_text("BEGIN:VCARD\nFN:A\nEND:VCARD\nBEGIN:VCARD\nFN:B\nEND:VCARD\n")^
let same = rfc.replace_scalar(loaded.doc, summary.node.first, summary.node.last, summary.node.text)^;
[
  same.doc.text == text,
  summary.name == "SUMMARY" and summary.node.text == "Folded value",
  len(event.children) == 4 and len(cards.doc.tree.children) == 2,
  contains(changed.doc.text, "SUMMARY;LANGUAGE=en:Folded \r\n changed value\r\n"),
  contains(renamed.doc.text, "DESCRIPTION;LANGUAGE=en:Folded \r\n value\r\n"),
  contains(added.doc.text, "END:VALARM\r\nLOCATION:Room 2\r\nEND:VEVENT"),
  not contains(deleted.doc.text, "CN=") and contains(deleted.doc.text, "ATTENDEE:mailto:b@"),
  not contains(alarm_deleted.doc.text, "VALARM") and contains(alarm_deleted.doc.text, "END:VEVENT"),
  (rfc.replace_scalar(loaded.doc, summary.node.first, summary.node.last, "a\nEND:VEVENT") ^ { null }) == null,
  (rfc.rename_key(loaded.doc, summary.first, summary.key_end, "END") ^ { null }) == null,
  (rfc.insert_child(loaded.doc, event.first, "BEGIN", "VEVENT") ^ { null }) == null,
  (rfc.import_text("BEGIN:VCALENDAR\nBEGIN:VEVENT\nEND:VCALENDAR\n") ^ { null }) == null,
  (rfc.vcf_descriptor.import_text("BEGIN:VCARD\nFN:A\n") ^ { null }) == null
]
