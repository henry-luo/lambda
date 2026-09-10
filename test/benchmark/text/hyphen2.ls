// Typed entry point for the shared Liang-pattern hyphenation benchmark.
// The core owns dynamic JSON trie traversal, which the library requires.
import test.benchmark.hyphen_core

pn main() {
    let completed = run_hyphen_benchmark()
}
