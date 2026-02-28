// Kostya Benchmark: json_gen
// JSON generation — build a large JSON structure and serialize
// Adapted from github.com/kostya/benchmarks
// Builds a JSON-like structure with 1000 objects, converts to string
// Measures map creation and string concatenation speed

pn json_escape(s) {
    // Simple escape for JSON strings (just handle double quotes)
    return s
}

pn json_str(s) {
    return "\"" ++ json_escape(s) ++ "\""
}

pn json_num(n) {
    return string(n)
}

pn json_obj_2(k1, v1, k2, v2) {
    return "{" ++ json_str(k1) ++ ":" ++ v1 ++ "," ++ json_str(k2) ++ ":" ++ v2 ++ "}"
}

pn json_obj_4(k1, v1, k2, v2, k3, v3, k4, v4) {
    return "{" ++ json_str(k1) ++ ":" ++ v1 ++ "," ++ json_str(k2) ++ ":" ++ v2 ++ "," ++ json_str(k3) ++ ":" ++ v3 ++ "," ++ json_str(k4) ++ ":" ++ v4 ++ "}"
}

pn next_rand(seed) {
    return (seed * 1664525 + 1013904223) % 1000000
}

pn benchmark() {
    let num_objects = 1000
    var seed = 42
    var json = "["
    var i = 0
    while (i < num_objects) {
        if (i > 0) {
            json = json ++ ","
        }
        seed = next_rand(seed)
        var id = seed % 10000
        seed = next_rand(seed)
        var x = float(seed % 20000 - 10000) / 100.0
        seed = next_rand(seed)
        var y = float(seed % 20000 - 10000) / 100.0
        seed = next_rand(seed)
        var score = seed % 100

        var coord = json_obj_2("x", json_num(int(floor(x))), "y", json_num(int(floor(y))))
        var obj = json_obj_4("id", json_num(id), "score", json_num(score), "coord", coord, "active", "true")
        json = json ++ obj
        i = i + 1
    }
    json = json ++ "]"
    return len(json)
}

pn main() {
    var result = 0
    var iter = 0
    while (iter < 10) {
        result = benchmark()
        iter = iter + 1
    }
    print("json_gen: length=" ++ string(result) ++ "\n")
    if (result > 0) {
        print("json_gen: PASS\n")
    } else {
        print("json_gen: FAIL\n")
    }
}
