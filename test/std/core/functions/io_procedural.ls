// Test: I/O Procedural Functions
// Layer: 2 | Category: function | Covers: print, cmd, clock
// Mode: procedural

pn main() {
    // ===== print =====
    print("hello from procedural")
    
    // ===== clock =====
    let t1 = clock()
    let sum = 0
    var i = 0
    while (i < 100) {
        i = i + 1
    }
    let t2 = clock()
    print("clock works: " ++ string(t2 >= t1))
    
    // ===== cmd =====
    let result = cmd("echo", "hello")
    print("cmd result: " ++ string(result))
}
