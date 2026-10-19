import img: lambda.image
pn main() {
    let empty = img.blank({width:520,height:16})^
    let blank_info = img.storage_info(empty)^
    let painted = img.paint_stroke(empty,{points:[[250.0,8.0],[264.0,8.0]],size:4.0,hardness:1.0,color:[1.0,0.0,0.0,1.0]})^
    let painted_info = img.storage_info(painted)^
    let next = img.paint_stroke(painted,{points:[[260.0,8.0]],size:2.0,hardness:1.0,color:[0.0,1.0,0.0,1.0]})^
    let next_info = img.storage_info(next)^
    return [blank_info.private_bytes == 0,
        painted_info.private_bytes == 32768 and painted_info.resident_references == 2,
        next_info.private_bytes == 16384 and next_info.shared_references == 1 and painted.g[8,260] == 0]
}
