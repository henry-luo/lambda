import m: ~~.mod_model
import raster: ~~.mod_raster
import scene: ~~.mod_scene
import image: lambda.image
import media: lambda.doc.media

// synthetic profiled screenshot, matching test/ui/sketch/fixtures/profiled.png.
let png = b'\64iVBORw0KGgoAAAANSUhEUgAAAIAAAABgCAYAAADVenpJAAABeGlDQ1BHZW5lcmF0ZWQgc1JHQgAAeJx1kT1Lw1AUht+2SkUrHXQQcchQxaEFURBHrUOXIqVWsOqS3CaNkKThJkGKq+DiUHAQXfwa/Ae6Cq4KgqAIIg7+Ar8WKfHcptAi7bncnIc35z2cnADhrMFMp2ceMC2X5zNpabW4JkXfEaLTCJk59kIul0XX+HkMah9Solf3uo4xUFIdBoT6iGeZzV1imgbZLdcWvEc8zHS5RHxCnOQ0IPGt0JWA3wSXA/4SzAv5RSAsekrlNlbamOncJJ4kTpiGx5rziC+JqdbKMuVRumNwkEcGaUhQ4GETBlykKFu0s86+qYZvCRXyMHraqIKTowydvElSPeqqUtZIV+kYqIq9/9+no81MB91jaaD31fc/x4HoPlCv+f7vqe/Xz4DIC3BttfwV2tPcN+m1lpY4BuI7wOVNS1MOgKtdYOTZlrnckCJ0w5oGfFwAg0Vg6B7oXw921XyP8yegsE2/6A44PAImqD6+8Qcw+WeqEpKM5gAAAORJREFUeJzt0iEBACAQwMCPQwjCEZQwxEDsxPmJzV370DW/AzAABsAAGAADYAAMgAEwAAbAABgAA2AADIABMAAGwAAYAANgAAyAATAABsAAGAADYAAMgAEwAAbAABgAA2AADIABMAAGwABxBogzQJwB4gwQZ4A4A8QZIM4AcQaIM0CcAeIMEGeAOAPEGSDOAHEGiDNAnAHiDBBngDgDxBkgzgBxBogzQJwB4gwQZ4A4A8QZIM4AcQaIM0CcAeIMEGeAOAPEGSDOAHEGiDNAnAHiDBBngDgDxBkgzgBxBogzQJwB4h5JzgTt6KgSZgAAAABJRU5ErkJggg=='
let decoded = image.decode(png)^
image.assets(decoded)[0].format=="png"
image.raster_of(image.layers(image.composition(decoded))[0]).color=="unknown"
image.render_frame(decoded) ^ { true } ~ { false }
let source = raster.from_bytes(png,"Screenshot.png")^
source.image_width==128 and source.image_height==96
media.data_uri_bytes(source.src)^==png
raster.from_uri(source.src)^.src==source.src
let empty = m.initial(m.blank())
let placed = m.add_image(empty,source)
len(placed.doc.layers)==2 and m.chosen(placed).kind=="image"
raster.validate(m.chosen(placed))^
let reopened = m.decode(parse(format(m.encode(placed.doc),'json'),'json')^)^
reopened==placed.doc
m.objects(m.open_document(empty,reopened).doc)[0].src==source.src
let pasted = m.paste(m.copy_selection(placed))
m.chosen(pasted).src==source.src and len(pasted.doc.layers)==3
m.step_history(pasted).doc==placed.doc
contains(format(scene.svg(placed.doc),'xml'),source.src)
raster.from_bytes(b'\x89504E470D0A1A0A') ^ { true } ~ { false }
