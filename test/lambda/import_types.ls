// test type_list isolation: main script and module both define map types
import .test.lambda.mod_types

// module's map types
let p = make_point(3, 4)
p

// main script's own map type (different shape from module's)
let config = {host: "localhost", port: 8080}
config

// module pub var (map created in module)
origin

// module function + main script map interaction
let person = make_person("Bob", 25)
person.name
person.age
