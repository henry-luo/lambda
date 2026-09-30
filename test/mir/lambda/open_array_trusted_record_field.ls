// D3.3.3v3/S8.2.4v3: an open array can hold an admitted record alongside
// another map or a non-map; the field read must guard the actual shape.
type IdleData = {control: int, icount: int}
type World = {datas: array}

pn read_count(world: World, key: int) any {
    return world.datas[key].icount
}

pn main() {
    var world: World = {datas: fill(3, null)}
    let idle: IdleData = {control: 1, icount: 7}
    world.datas[0] = idle
    world.datas[1] = {icount: 17}
    world.datas[2] = [1]
    print([read_count(world, 0), read_count(world, 1),
           read_count(world, 2), read_count(world, 9)])
    print("\n")
}
