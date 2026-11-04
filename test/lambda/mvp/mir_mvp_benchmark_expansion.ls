import ._mir_mvp_expansion_dependency

type Box = {values: int[], extra: array}

fn word_or_empty(words: string[], index: int) string =>
    if (index < len(words)) words[index] else ""

fn raised_number(n: int) int^ { raise error(string(n)) }

fn default_local(value: int, other: int = value + 2) int => value + other

fn allocated_box(n: int) Box^ => {values: [n, n + 1], extra: []}

pn main() {
    print("module: " ++ first_seed() ++ " " ++ default_external(3) ++ " " ++ dependency_rows[0][1] ++ "\n")
    print("defaults: " ++ default_local(3) ++ " " ++ default_local(3, 8) ++ "\n")
    let admitted_box: Box = allocated_box(1)^
    var box: Box = admitted_box
    let saved: Box = box
    replace_first(box.values, 7)^
    box.extra.push([9])^
    print("places: " ++ saved.values[0] ++ " " ++ box.values[0] ++ " " ++ len(saved.extra) ++ " " ++ len(box.extra) ++ "\n")

    var dynamic: array = []
    dynamic.push(0)^
    dynamic[0] = 9
    print("open-array: " ++ dynamic[0] ++ "\n")
    print("keys: " ++ join([for (key, item at {a: 2, b: 3}) string(key) ++ string(item)], ",") ++ "\n")
    let text: string = "λx"
    print("text: " ++ join([for (index of text) chr(ord(text[index]))], "") ++ "\n")
    let empty = [for (item in [1, 2] where item > 3) item]
    print("empty: " ++ len(empty) ++ "\n")
    let ranked = sort([1, 3, 2], (item) => 0 - item)
    print("sort: " ++ ranked[0] ++ " " ++ ranked[1] ++ " " ++ ranked[2] ++ "\n")
    let groups = [for (item in [2, 1, 2] group by item as key into members order by key)
        {key: key, count: len(members)}]
    print("groups: " ++ groups[0].key ++ ":" ++ groups[0].count ++ " " ++ groups[1].key ++ ":" ++ groups[1].count ++ "\n")
    let computed = {[string(3)]: 4, plain: 5}
    print("map: " ++ computed["3"] ++ " " ++ computed.plain ++ "\n")
    var dictionary: map = {}
    dictionary[string(3)] = 4
    let previous = dictionary
    dictionary["3"] = 6
    print("map-write: " ++ len(dictionary) ++ " " ++ previous["3"] ++ " " ++ dictionary["3"] ++ "\n")
    print("branch: " ++ word_or_empty(["kept"], 0) ++ ":" ++ word_or_empty(["kept"], 2) ++ "\n")
    print("recovery: " ++ join([for (n in [2, 3]) string(raised_number(n) ^ { int(^.message) ^ { 0 } })], ",") ++ "\n")
    print("membership: " ++ ("key" at {key: 1}) ++ " " ++ (1 in [0, 1]) ++ " " ++ (0 of [4]) ++ "\n")
    print("observer: " ++ (type(error("observed")) == error) ++ " " ++ contains("lambda", "mb") ++ "\n")
}
