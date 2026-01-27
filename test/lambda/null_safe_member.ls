// Test file for null-safe member access
// The '.' operator returns null when the object is null (like JavaScript's ?.)

// Test 1: Direct null member access
"Test 1 - direct null member access:"
let x = null
"x.a ="; x.a
"x.foo ="; x.foo

// Test 2: Chained null member access
"Test 2 - chained null member access:"
"x.a.b.c ="; x.a.b.c

// Test 3: Null in nested map
"Test 3 - null in nested map:"
let m = {a: null, b: {c: 1}}
"m.a ="; m.a
"m.a.foo ="; m.a.foo
"m.b.c ="; m.b.c

// Test 4: Deep nesting with null
"Test 4 - deep nesting with null:"
let deep = {level1: {level2: null}}
"deep.level1.level2 ="; deep.level1.level2
"deep.level1.level2.level3 ="; deep.level1.level2.level3
"deep.level1.level2.level3.level4 ="; deep.level1.level2.level3.level4

// Test 5: Missing field returns null (not error)
"Test 5 - missing field returns null:"
let obj = {a: 1, b: 2}
"obj.a ="; obj.a
"obj.c ="; obj.c
"obj.missing ="; obj.missing

// Test 6: Element with null attribute
"Test 6 - element with null attribute:"
let elm = <elmt a: 1, b: null, c: 3>
"elm.a ="; elm.a
"elm.b ="; elm.b
"elm.b.foo ="; elm.b.foo
"elm.c ="; elm.c

// Test 7: Method calls on null should return null (for method-style calls)
"Test 7 - method style calls on null:"
let arr = null
"arr.len() ="; arr.len()  // len(null) returns 0, which is correct

// Test 8: Index access on null
"Test 8 - index access on null:"
let my_list = null
"my_list[0] ="; my_list[0]
"my_list[1] ="; my_list[1]

// Test 9: Null propagation pattern
"Test 9 - null propagation pattern:"
let user = {name: "Alice", address: null}
"user.name ="; user.name
"user.address ="; user.address
"user.address.city ="; user.address.city

// Test 10: Comparison with null
"Test 10 - comparison with null result:"
let data = {x: null}
"data.x == null ="; data.x == null
"data.x.y == null ="; data.x.y == null
"data.missing == null ="; data.missing == null

// Test 11: Null propagation through multiple map accesses
"Test 11 - null propagation through map chain:"
let config = {
    db: {
        host: "localhost",
        port: 5432,
        ssl: null
    },
    cache: null
}
"config.db.host ="; config.db.host
"config.db.ssl ="; config.db.ssl
"config.db.ssl.cert ="; config.db.ssl.cert
"config.cache ="; config.cache
"config.cache.size ="; config.cache.size

// Test 12: Array of maps with null values
"Test 12 - array with null and non-null maps:"
let items = [{id: 1, name: "A"}, null, {id: 3, name: "C"}]
"items[0].name ="; items[0].name
"items[1] ="; items[1]
"items[1].name ="; items[1].name
"items[2].name ="; items[2].name
