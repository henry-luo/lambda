// Object JSON serialization with "@" type discriminator
type Point { x: int, y: int }
let p = {Point x: 3, y: 4}
format(p, "json")
