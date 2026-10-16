import img: lambda.image
import edits: lambda.image.edit

let red = img.blank({width: 3, height: 2, color: [1.0, 0.0, 0.0, 1.0]})^
let blue = img.blank({width: 1, height: 1, color: [0.0, 0.0, 1.0, 1.0]})^
let changed = img.with_region(red, [1, 0, 2, 1], blue.pixels)^
; [red.r[0, 1], changed.r[0, 1], changed.b[0, 1], changed.r[1, 1]]
let transparent = img.with_channel(changed, 'a', img.clear(red)^.a)^
; [transparent.a[0, 1], changed.a[0, 1]]
let composition = img.stack([img.layer(blue, {id: 'front'}), img.layer(red, {id: 'back'})])
let reversed = img.reorder(composition, 0, 1)^
; [img.layers(reversed)[0].id, img.layers(composition)[0].id]
let doc = img.document(composition, {width: 3, height: 2})
let moved = img.replace_at(doc, [0, 0], img.with_properties(doc[0][0], {position: [2, 1]})^)^
; [moved[0][0].position, doc[0][0].position, img.render_frame(moved)^.b[1, 2]]
let session = edits.session(doc)
let one = edits.record(session, moved, 'stroke')
let two = edits.record(one, img.replace_at(moved, [0, 0, 0], red)^, 'stroke')
let undone = edits.undo(two)
let redone = edits.redo(undone)
; [len(two.hist.undo), undone.doc == doc, redone.doc == two.doc, edits.can_redo(undone)^]
let anim = img.to_animation(doc, {ticks: 1, rate: 10})^
let second = img.insert(anim, 1, img.frame(img.stack([img.layer(red)]), {ticks: 2, rate: 10}))^
let edited = img.replace_at(second, [0, 0, 0, 0], red)^
; [len(img.frames(second)), img.render_frame(second, {frame: 0})^.b[0, 0],
   img.render_frame(edited, {frame: 0})^.b[0, 0], img.frames(second)[1] == img.frames(edited)[1]]
let stamped = img.stamp(red, blue, [1, 1])^
; [stamped.b[1, 1], stamped.r[1, 2]]
; [img.validate(doc)^, img.info(doc).width, img.adopt(img.materialize(red))^ == red]

let reported = img.encode_report(doc, {format: 'psd'})^
; [reported.bytes is binary, reported.report.regenerated, reported.report.rasterized, reported.report.omitted]
; [len([for (format in img.capabilities()^ where format.format == "psd") format]),
   img.sample(red, 1, 0)^, img.render_region(doc, [1, 0, 3, 1])^.width]

let psd_source = img.decode(reported.bytes)^
let source_session = edits.session(psd_source)
; [edits.original_bytes(source_session)^ == reported.bytes,
   (edits.original_bytes(edits.record(source_session, moved)) ^ { true }),
   img.rotate(red, 1)^.width, img.rotate(red, 1)^.height]

; [img.read_region(red, [0, 0, 1, 1], {channel: "r"})^[0, 0],
   (img.read_region(red, [0, 0, 1, 1], {channe: "r"}) ^ { true }),
   (img.from_array(red.pixels, {width: 99}) ^ { true }),
   (img.stamp(red, blue, [0, 0], {blend: 'multiply'}) ^ { true })]

; [(img.encode(doc, {format: 'psd', quality: 90}) ^ { true }),
   (img.render_frame(red, {frame: 1}) ^ { true })]

; img.adopt(<raster *: img.materialize(red), alpha_mode: 'premultiplied'>) ^ { true }

let invalid_empty = img.document(img.stack([<layer kind: 'empty', blue>]), {width: 1, height: 1})
; [(img.render_frame(invalid_empty) ^ { true }), (img.encode(invalid_empty, {format: 'psd'}) ^ { true })]
