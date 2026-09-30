// A live local int lane survives a borrowed call and a nullable indexed read.
type Link = {next: int}
type Cursor = {position: int, links: Link?[]}

pn read_position(var cursor: Cursor) int {
    return cursor.position
}

pn advance(var cursor: Cursor) int {
    var position = cursor.position
    cursor.position = position
    let observed = read_position(cursor)
    let next = cursor.links[observed].next
    position = next
    cursor.position = position
    return cursor.position
}

pn possibly_null(var cursor: Cursor, index: int) int {
    cursor.position = cursor.links[index].next
    return cursor.position
}

pn possibly_null_local(var cursor: Cursor, index: int) int {
    var next = cursor.links[index].next
    cursor.position = next
    return cursor.position
}

pn main() {
    var cursor: Cursor = {position: 0,
        links: [{next: 1}, {next: 1}]}
    print([advance(cursor), cursor.position,
        possibly_null(cursor, 0), possibly_null_local(cursor, 0)])
    print("\n")
    var absent: Cursor = {position: 0, links: [null]}
    print([advance(absent), absent.position,
        possibly_null(absent, 0), possibly_null_local(absent, 0)])
}
