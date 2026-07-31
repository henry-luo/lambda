/* Native C2MIR port of larceny/gcbench.ls. */
extern int printf(const char *, ...);
#include "../../c2mir/tree_benchmark.h"

int main(void) {
    int depth;
    C2MirTreeNode *stretch = c2mir_make_tree(15);
    C2MirTreeNode *long_lived;
    printf("stretch tree of depth 15 check: %d\n", c2mir_check_tree(stretch));
    c2mir_free_tree(stretch);
    long_lived = c2mir_make_tree(14);
    for (depth = 4; depth <= 14; depth += 2) {
        int i;
        int iterations = 1 << (14 - depth + 4);
        int total = 0;
        for (i = 0; i < iterations; i++) {
            C2MirTreeNode *tree = c2mir_make_tree(depth);
            total += c2mir_check_tree(tree);
            c2mir_free_tree(tree);
        }
        printf("%d trees of depth %d check: %d\n", iterations, depth, total);
    }
    printf("long lived tree of depth 14 check: %d\n", c2mir_check_tree(long_lived));
    c2mir_free_tree(long_lived);
    return 0;
}
