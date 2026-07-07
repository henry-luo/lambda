// Phase 3: JSON object keys are strings, so the empty key round-trips as "".

'=== json empty key ==='
let obj^err = parse("{\"\":1,\"regular\":2}", 'json')
obj[""]
obj["regular"]
obj[""] == 1
obj["missing"] == null

'=== round trip ==='
let encoded = format(obj, 'json')
encoded
let round^err2 = parse(encoded, 'json')
round[""] == 1
round["regular"] == 2
