// Test starts_with, ends_with, and contains builtins
// Validates correct boolean returns including in direct if-condition usage

"===== starts_with tests ====="

"basic prefix match:"
starts_with("hello world", "hello")

"prefix no match:"
starts_with("hello world", "world")

"prefix longer than string:"
starts_with("hi", "hello")

"exact match:"
starts_with("hello", "hello")

"with slash - should not match:"
starts_with("test_strbuf.cpp", "test/")

"with slash - should match:"
starts_with("test/something.cpp", "test/")

"single char prefix:"
starts_with("hello", "h")

"===== ends_with tests ====="

"basic suffix match:"
ends_with("hello world", "world")

"suffix no match:"
ends_with("hello world", "hello")

"extension .cpp:"
ends_with("main.cpp", ".cpp")

"extension .c:"
ends_with("main.c", ".c")

".c should not match .cpp:"
ends_with("main.cpp", ".c")

"exact match:"
ends_with("hello", "hello")

"===== starts_with in if-condition ====="

// critical: starts_with returning false must be falsy in if
fn check_prefix(s, prefix) {
    if (starts_with(s, prefix)) "yes" else "no"
}

"test/ on test_foo.cpp:"
check_prefix("test_foo.cpp", "test/")

"test/ on test/foo.cpp:"
check_prefix("test/foo.cpp", "test/")

"src/ on src/main.cpp:"
check_prefix("src/main.cpp", "src/")

"src/ on lib/main.cpp:"
check_prefix("lib/main.cpp", "src/")

"===== ends_with in if-condition ====="

fn check_suffix(s, suffix) {
    if (ends_with(s, suffix)) "yes" else "no"
}

".cpp on main.cpp:"
check_suffix("main.cpp", ".cpp")

".cpp on main.c:"
check_suffix("main.c", ".cpp")

".c on main.c:"
check_suffix("main.c", ".c")

".c on main.cpp:"
check_suffix("main.cpp", ".c")

"===== contains in if-condition ====="

fn check_contains(s, sub) {
    if (contains(s, sub)) "yes" else "no"
}

"world in hello world:"
check_contains("hello world", "world")

"xyz in hello world:"
check_contains("hello world", "xyz")

"===== ALL TESTS DONE ====="
