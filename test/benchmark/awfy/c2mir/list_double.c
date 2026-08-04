/* Native C2MIR double port of awfy/list2.ls.  List lengths are double; pointers are unchanged. */
extern int printf(const char *, ...);
extern void *malloc(unsigned long);

typedef struct Node { struct Node *next; } Node;

static Node *make_list(double length) {
    Node *node;
    if (length == 0.0) return 0;
    node = (Node *) malloc(sizeof(Node));
    if (node == 0) return 0;
    node->next = make_list(length - 1.0);
    return node;
}

static double list_length(Node *node) { return node == 0 ? 0.0 : 1.0 + list_length(node->next); }

static double is_shorter_than(Node *x, Node *y) {
    while (y != 0) {
        if (x == 0) return 1.0;
        x = x->next;
        y = y->next;
    }
    return 0.0;
}

static Node *tail_list(Node *x, Node *y, Node *z) {
    if (is_shorter_than(y, x) != 0.0) return tail_list(tail_list(x->next, y, z), tail_list(y->next, z, x), tail_list(z->next, x, y));
    return z;
}

int main(void) {
    Node *result = tail_list(make_list(15.0), make_list(10.0), make_list(6.0));
    double length = list_length(result);
    printf(length == 10.0 ? "List: PASS\n" : "List: FAIL result=%.0f\n", length);
    return length != 10.0;
}
