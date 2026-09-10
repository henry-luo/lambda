// D3.2.4v3 / D3.3.3v3: runtime indices retain named-record layout proofs.
type Row = {value: int, values: int[]}
type World = {rows: Row?[], counter: int}

pn bump(var world: World, index: int) any {
    world.rows[index].value = world.rows[index].value + 1
}

pn write(var world: World, index: int, value) any {
    world.rows[index].value = value
}

pn append(var world: World, index: int) any {
    var values: int[] = world.rows[index].values
    push(values, 9)
    world.rows[index].values = values
}

pn next_index(var world: World) int {
    world.counter = world.counter + 1
    return 0
}

pn write_element(var world: World, index: int, value: int) any {
    world.rows[0].values[index] = value
}

pn replace_values(var world: World, index: int, values) any {
    world.rows[index].values = values
}

pn main() {
    var world: World = {rows: [{value: 4, values: [1]}, null], counter: 0}
    let original = world
    bump(world, 0)
    append(world, 0)
    bump(world, 0)
    // Both a shared spine and its now-unique successor preserve COW snapshots.
    print([original.rows[0].value, world.rows[0].value,
        len(original.rows[0].values), len(world.rows[0].values)])
    print("\n")
    print([world.rows[1].value is null, world.rows[2].value is null,
        world.rows[-1].value is null, world.rows[1].values[0] is null])
    print("\n")
    var bad_value = false
    var bad_index = false
    var bad_null = false
    write(world, 0, "wrong") ^ { bad_value = true }
    write(world, 2, 8) ^ { bad_index = true }
    write(world, 1, 8) ^ { bad_null = true }
    print([bad_value, bad_index, bad_null, world.rows[0].value])
    print("\n")
    // Keys are evaluated once, including the cold/shared-store arm.
    let before_key = world
    world.rows[next_index(world)].value = 12
    print([before_key.rows[0].value, world.rows[0].value, world.counter])
    print("\n")
    let before_replace = world
    world.rows[0] = {value: 15, values: [7]}
    bump(world, 0)
    print([before_replace.rows[0].value, world.rows[0].value])
    print("\n")
    write_element(world, 0, 9007199254740991)
    var bad_element = false
    write_element(world, 3, 2) ^ { bad_element = true }
    print([world.rows[0].values[0], bad_element])
    print("\n")
    var replacement: int[] = [2, 3]
    replace_values(world, 0, replacement)
    push(replacement, 4)
    append(world, 0)
    var rejected_array = false
    replace_values(world, 0, ["wrong"]) ^ { rejected_array = true }
    print([world.rows[0].values, replacement, rejected_array])
    print("\n")
}
