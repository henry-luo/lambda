/* Native C2MIR port of jetstream/splay.ls. */
extern int printf(const char *, ...);
extern void *malloc(unsigned long);
extern void free(void *);

#define TREE_SIZE 8000
#define TREE_MODIFICATIONS 80

/* Payload tree from generate_payload(5, key): internal nodes carry two
 * children, leaves carry a 10-element array and the key tag. */
typedef struct Payload {
    struct Payload *left_p;
    struct Payload *right_p;
    long arr[10];
    double tag;
} Payload;

typedef struct Node {
    double key;
    struct Node *left;
    struct Node *right;
    Payload *value;
} Node;

static Node *root;
static unsigned rng_seed;

/* LCG matching the .ls next_random: u32 wraparound, then scale to [0,1). */
static double next_random(void) {
    rng_seed = rng_seed * 1103515245u + 12345u;
    return (double) rng_seed / 4294967296.0;
}

static Node *create_node(double key, Payload *value) {
    Node *n = (Node *) malloc(sizeof(Node));
    n->key = key;
    n->left = 0;
    n->right = 0;
    n->value = value;
    return n;
}

static Payload *generate_payload(int depth, double tag) {
    Payload *p = (Payload *) malloc(sizeof(Payload));
    if (depth == 0) {
        int i;
        p->left_p = 0;
        p->right_p = 0;
        for (i = 0; i < 10; i++) p->arr[i] = i;
        p->tag = tag;
    } else {
        p->left_p = generate_payload(depth - 1, tag);
        p->right_p = generate_payload(depth - 1, tag);
        p->tag = 0.0;
    }
    return p;
}

static void free_payload(Payload *p) {
    if (p != 0) {
        free_payload(p->left_p);
        free_payload(p->right_p);
        free(p);
    }
}

/* Top-down splay with dummy header, mirroring the .ls splay() control flow. */
static void splay(double key) {
    Node dummy_node;
    Node *dummy = &dummy_node;
    Node *left, *right, *current;
    int done = 0;
    if (root == 0) return;
    dummy->key = 0.0;
    dummy->left = 0;
    dummy->right = 0;
    left = dummy;
    right = dummy;
    current = root;
    while (!done) {
        if (key < current->key) {
            if (current->left == 0) {
                done = 1;
            } else {
                if (key < current->left->key) {
                    Node *tmp = current->left;
                    current->left = tmp->right;
                    tmp->right = current;
                    current = tmp;
                    if (current->left == 0) done = 1;
                }
                if (!done) {
                    right->left = current;
                    right = current;
                    current = current->left;
                }
            }
        } else if (key > current->key) {
            if (current->right == 0) {
                done = 1;
            } else {
                if (key > current->right->key) {
                    Node *tmp = current->right;
                    current->right = tmp->left;
                    tmp->left = current;
                    current = tmp;
                    if (current->right == 0) done = 1;
                }
                if (!done) {
                    left->right = current;
                    left = current;
                    current = current->right;
                }
            }
        } else {
            done = 1;
        }
    }
    left->right = current->left;
    right->left = current->right;
    current->left = dummy->right;
    current->right = dummy->left;
    root = current;
}

static void splay_insert(double key, Payload *value) {
    Node *node;
    if (root == 0) {
        root = create_node(key, value);
        return;
    }
    splay(key);
    if (root->key == key) return;
    node = create_node(key, value);
    if (key > root->key) {
        node->left = root;
        node->right = root->right;
        root->right = 0;
    } else {
        node->right = root;
        node->left = root->left;
        root->left = 0;
    }
    root = node;
}

static Node *splay_remove(double key) {
    Node *removed;
    if (root == 0) return 0;
    splay(key);
    if (root->key != key) return 0;
    removed = root;
    if (root->left == 0) {
        root = root->right;
    } else {
        Node *right_tree = root->right;
        root = root->left;
        splay(key);
        root->right = right_tree;
    }
    return removed;
}

static Node *splay_find(double key) {
    if (root == 0) return 0;
    splay(key);
    return root->key == key ? root : 0;
}

static Node *splay_find_max(Node *node) {
    while (node->right != 0) node = node->right;
    return node;
}

static Node *splay_find_greatest_less_than(double key) {
    if (root == 0) return 0;
    splay(key);
    if (root->key < key) return root;
    if (root->left != 0) return splay_find_max(root->left);
    return 0;
}

static long count_nodes(Node *node) {
    if (node == 0) return 0;
    return 1 + count_nodes(node->left) + count_nodes(node->right);
}

static double insert_new_node(void) {
    double key = next_random();
    while (splay_find(key) != 0) key = next_random();
    splay_insert(key, generate_payload(5, key));
    return key;
}

int main(void) {
    long node_count;
    int i, iter, j;
    root = 0;
    rng_seed = 49734321u;
    for (i = 0; i < TREE_SIZE; i++) insert_new_node();
    for (iter = 0; iter < 50; iter++) {
        for (j = 0; j < TREE_MODIFICATIONS; j++) {
            double key = insert_new_node();
            Node *greatest = splay_find_greatest_less_than(key);
            Node *removed;
            if (greatest == 0) removed = splay_remove(key);
            else removed = splay_remove(greatest->key);
            if (removed != 0) { /* GC would reclaim these in the .ls */
                free_payload(removed->value);
                free(removed);
            }
        }
    }
    node_count = count_nodes(root);
    if (node_count == TREE_SIZE) {
        printf("splay: PASS (nodes=%ld)\n", node_count);
        return 0;
    }
    printf("splay: FAIL (nodes=%ld, expected %d)\n", node_count, TREE_SIZE);
    return 1;
}
