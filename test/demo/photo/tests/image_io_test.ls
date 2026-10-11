import files: ~~.mod_io
import model: ~~.mod_model
let jpeg = input("test/demo/scene3d/assets/stone.jpg",'binary')^
let raw = load(jpeg)^
let orientations = [b'\x0100',b'\x0200',b'\x0300',b'\x0400',b'\x0500',b'\x0600',b'\x0700',b'\x0800']
fn tagged(bytes,tag) => slice(bytes,0,2) ++
    b'\xFFE1002245786966000049492A000800000001001201030001000000' ++
    tag ++ b'\x000000000000' ++ slice(bytes,2)
shape(raw) == [128,128,4]
load({data:jpeg,max_pixels:1}) ^ { "limited" } == "limited"
load(b'\x000102') ^ { "malformed" } == "malformed"
files.fingerprint(b'\x010203') == files.fingerprint(b'\x010203')
files.fingerprint(b'\x010203') != files.fingerprint(b'\x010302')
let expected = [raw,flip(raw,1),rot90(raw,2),flip(raw,0),rot90(flip(raw,1),1),rot90(raw,3),rot90(flip(raw,1),3),rot90(raw,1)]
all([for (i,tag in orientations) load({data:tagged(jpeg,tag),max_pixels:16000000,normalize_orientation:true}) == expected[i]])
let malicious = slice(jpeg,0,2) ++ b'\xFFE1001045786966000049492A00FFFFFFFF' ++ slice(jpeg,2)
load({data:malicious,normalize_orientation:true}) == raw
