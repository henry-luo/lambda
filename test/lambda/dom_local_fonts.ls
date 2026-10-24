import dom

let faces = dom.query_local_fonts(null)^
len(faces)>0
all([for (face in faces) face.postscriptName is string and face.fullName is string and
    face.family is string and face.style is string and face.postscriptName!="" and face.family!=""])
let names = [for (face in faces) face.postscriptName]
names == sort(unique(names))
let selected = dom.query_local_fonts({postscriptNames:[faces[0].postscriptName]})^
len(selected)==1 and selected[0]==faces[0]
let empty_faces = dom.query_local_fonts({postscriptNames:[]})^
empty_faces == []
let missing_faces = dom.query_local_fonts({postscriptNames:["LambdaMissingNativeFace-e33149"]})^
missing_faces == []
let invalid = dom.query_local_fonts({postscriptNames:17}) ^ {null}
invalid == null
