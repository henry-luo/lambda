type World = {tasks: array, count: int}
type Strict = {tasks: {value: int}[], count: int}

fn dynamic(value) any => value

pn update(var world: World) {
    var i = 0
    while (i < 20) {
        world.tasks[0].value = i
        world.tasks[0].ready = true
        i = i + 1
    }
}

pn rejected(var world: Strict) any^ {
    world.tasks[0].value = dynamic(3.5)
}

pn main() {
    var world: World = {tasks: [{value: 0, ready: false}], count: 1}
    let before = world
    update(world)
    var strict: Strict = {tasks: [{value: 7}], count: 1}
    let strict_before = strict
    var failed = false
    rejected(strict) ^ { failed = true }
    print([before.tasks[0].value, before.tasks[0].ready,
        world.tasks[0].value, world.tasks[0].ready,
        failed, strict.tasks[0].value, strict_before.tasks[0].value])
}
