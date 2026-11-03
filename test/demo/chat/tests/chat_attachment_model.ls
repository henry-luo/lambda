import a: lambda.chat.attachment
import media: lambda.doc.media
import model: lambda.chat.model
import img: lambda.image
let zipped = b'\64UEsDBBQAAAAIAAAAIQD8TNHQDwAAAA0AAAAJAAAAaGVsbG8udHh080jNyclX8EnMTUpJ5AIAUEsBAhQDFAAAAAgAAAAhAPxM0dAPAAAADQAAAAkAAAAAAAAAAAAAAIABAAAAAGhlbGxvLnR4dFBLBQYAAAAAAQABADcAAAA2AAAAAAA='
let zipped_file = a.checked({kind:"file", name:"hello.txt.zip", original_name:"hello.txt", mime:"application/zip",
    size:len(zipped), source_size:13, data:media.data_uri(zipped,"application/zip")^})^
let raw = input("test/ui/note/fixtures/clipboard.png",'binary')^
let uri = media.data_uri(raw,"image/png")^
let compressed = a.pasted_image(uri)^
let original = a.pasted_image(uri,false)^
let wide = img.encode(img.blank({width:2000,height:1000,color:[0.4,0.5,0.6,1]})^,{format:'png'})^
let resized = a.pasted_image(media.data_uri(wide,"image/png")^)^
let accepted = model.accept([],"alex",{id:"file",to:"maya",text:"",attachment:zipped_file},"12:00")
let duplicate = model.accept(accepted.messages,"alex",{id:"file",to:"maya",text:"",attachment:zipped_file},"12:01")
let collision = model.accept(accepted.messages,"alex",{id:"file",to:"maya",text:"",attachment:compressed},"12:01")
let invalid_name = a.checked({*:zipped_file,name:"../hello.txt.zip"}) ^ { ^ }
let invalid_size = a.checked({*:zipped_file,size:zipped_file.size+1}) ^ { ^ }
let invalid_zip = a.checked({*:zipped_file,data:media.data_uri(b'000102',"application/zip")^,size:3}) ^ { ^ }
let invalid_image = a.checked({*:compressed,width:1}) ^ { ^ }
let oversized = a.checked({*:zipped_file,size:a.MAX_BYTES+1}) ^ { ^ };
[a.bytes(zipped_file)^ == zipped, accepted.error == null, duplicate.messages == accepted.messages,
 collision.error != null, model.preview(accepted.messages,"alex","maya"),
 compressed.mime, compressed.width, compressed.height, compressed.size < len(raw),
 a.bytes(original)^ == raw, invalid_name is error, invalid_size is error, invalid_zip is error,
 invalid_image is error, oversized is error, model.history_batches([])^ == [[]], resized.width, resized.height]
