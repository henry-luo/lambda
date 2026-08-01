/* Native C2MIR port of beng/binarytrees.ls. */
extern int printf(const char *, ...);
#include "../../c2mir/tree_benchmark.h"
int main(void) {
    int depth; C2MirTreeNode *stretch = c2mir_make_tree(11); C2MirTreeNode *long_lived; int stretch_check = c2mir_check_tree(stretch);
    printf("stretch tree of depth 11\t check: %d\n", stretch_check); c2mir_free_tree(stretch);
    long_lived = c2mir_make_tree(10);
    for (depth = 4; depth <= 10; depth += 2) { int i; int iterations = 1 << (10 - depth + 4); int total = 0; for (i = 0; i < iterations; i++) { C2MirTreeNode *tree = c2mir_make_tree(depth); total += c2mir_check_tree(tree); c2mir_free_tree(tree); } printf("%d\t trees of depth %d\t check: %d\n", iterations, depth, total); }
    printf("long lived tree of depth 10\t check: %d\n", c2mir_check_tree(long_lived)); c2mir_free_tree(long_lived); return 0;
}
