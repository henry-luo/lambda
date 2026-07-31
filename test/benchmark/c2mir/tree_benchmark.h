/* Shared binary-tree allocation/check helpers for standalone native C2MIR ports. */
extern void *malloc(unsigned long);
extern void free(void *);

typedef struct C2MirTreeNode {
    struct C2MirTreeNode *left;
    struct C2MirTreeNode *right;
} C2MirTreeNode;

static C2MirTreeNode *c2mir_make_tree(int depth) {
    C2MirTreeNode *node = (C2MirTreeNode *) malloc(sizeof(C2MirTreeNode));
    if (node == 0) return 0;
    if (depth == 0) {
        node->left = 0;
        node->right = 0;
    } else {
        node->left = c2mir_make_tree(depth - 1);
        node->right = c2mir_make_tree(depth - 1);
    }
    return node;
}

static int c2mir_check_tree(const C2MirTreeNode *node) {
    return node->left == 0 ? 1 : 1 + c2mir_check_tree(node->left) + c2mir_check_tree(node->right);
}

static void c2mir_free_tree(C2MirTreeNode *node) {
    if (node != 0) {
        c2mir_free_tree(node->left);
        c2mir_free_tree(node->right);
        free(node);
    }
}
