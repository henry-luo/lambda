// D3.3.3v3/S8.2.4v3: a local key may select one admitted record or a collection.
// The field fast arm requires the actual record shape; all other keys fall back.
type Entry = {value: int}
type Position = {index: int}
type World = {entries: Entry[], positions: Position?[]}
type Task = {link: int, identity: int, priority: int, input: int,
             pp: bool, tw: bool, th: bool, fn_id: int}

pn local_key(world: World, position: int) any {
    var key = world.positions[position].index
    return world.entries[key].value
}

pn selected_key(entries: Entry[]) any {
    return entries[[0]].value
}

pn nullable_position(positions: Position?[], index: int) any {
    return positions[index].index
}

pn task_flag(tasks: Task?[], index: int) any {
    return tasks[index].th
}

pn task_id(tasks: Task?[], index: int) any {
    return tasks[index].fn_id
}

pn main() {
    let entries: Entry[] = [{value: 7}]
    let world: World = {entries: entries, positions: [{index: 0}]}
    let mixed: Position?[] = [null, {index: 0}]
    print([local_key(world, 0), local_key(world, 2),
           selected_key(entries), nullable_position(mixed, 0),
           nullable_position(mixed, 1), nullable_position(mixed, 3)])
    print("\n")
    let tasks: Task?[] = [{link: 0, identity: 1, priority: 2, input: -1,
                           pp: false, tw: false, th: true, fn_id: 3}]
    print([task_flag(tasks, 0), task_flag(tasks, 3)])
    print("\n")
    print([task_id(tasks, 0), task_id(tasks, 3)])
    print("\n")
    let assigned: Task = {link: 0, identity: 1, priority: 2, input: -1,
                          pp: false, tw: false, th: true, fn_id: -7}
    let admitted: Task?[] = [assigned]
    print([task_id(admitted, 0), task_id(admitted, 3)])
    print("\n")
}
