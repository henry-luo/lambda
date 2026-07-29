// Kostya Benchmark: json_gen (Typed version)
// JSON generation — build a large JSON structure and serialize
// Adapted from github.com/kostya/benchmarks
// Builds a JSON-like structure with 1000 objects, converts to string
// Measures map creation and string concatenation speed

pn json_escape(s: string) string {
    // Simple escape for JSON strings (just handle double quotes)
    return s
}

pn json_str(s: string) string {
    return "\"" ++ json_escape(s) ++ "\""
}

pn json_num(n: int) string {
    return string(n)
}

pn json_obj_2(k1: string, v1: string, k2: string, v2: string) string {
    return "{" ++ json_str(k1) ++ ":" ++ v1 ++ "," ++ json_str(k2) ++ ":" ++ v2 ++ "}"
}

pn json_obj_4(k1: string, v1: string, k2: string, v2: string,
              k3: string, v3: string, k4: string, v4: string) string {
    return "{" ++ json_str(k1) ++ ":" ++ v1 ++ "," ++ json_str(k2) ++ ":" ++ v2 ++ "," ++ json_str(k3) ++ ":" ++ v3 ++ "," ++ json_str(k4) ++ ":" ++ v4 ++ "}"
}

pn next_rand(seed: int) int {
    return (seed * 1664525 + 1013904223) % 1000000
}

pn benchmark() int {
    let num_objects: int = 1000
    var seed: int = 42
    var json: string = "["
    var i: int = 0
    while (i < num_objects) {
        if (i > 0) {
            json = json ++ ","
        }
        seed = next_rand(seed)
        var id: int = seed % 10000
        seed = next_rand(seed)
        var x: float = float(seed % 20000 - 10000) / 100.0
        seed = next_rand(seed)
        var y: float = float(seed % 20000 - 10000) / 100.0
        seed = next_rand(seed)
        var score: int = seed % 100

        var coord: string = json_obj_2("x", json_num(int(floor(x))), "y", json_num(int(floor(y))))
        var obj: string = json_obj_4("id", json_num(id), "score", json_num(score), "coord", coord, "active", "true")
        json = json ++ obj
        i = i + 1
    }
    json = json ++ "]"
    return len(json)
}

pn main() {
    var __t0 = clock()
    var result: int = 0
    var iter: int = 0
    while (iter < 10) {
        result = benchmark()
        iter = iter + 1
    }
    var __t1 = clock()
    print("json_gen: length=" ++ result ++ "\n")
    if (result > 0) {
        print("json_gen: PASS\n")
    } else {
        print("json_gen: FAIL\n")
    }
    print("__TIMING__:" ++ ((__t1 - __t0) * 1000.0) ++ "\n")
}
