#include "transpiler.h"

void test_transpiler_access() {
    Transpiler tp;
    // Test direct access to Script fields
    tp.source = NULL;
    tp.ast_pool = NULL;
    tp.current_scope = NULL;
    tp.const_list = NULL;
    tp.type_list = NULL;
    tp.syntax_tree = NULL;
}
