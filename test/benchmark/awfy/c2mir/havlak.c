/* Native C2MIR port of awfy/havlak.ls. */
extern int printf(const char *, ...);
extern void *malloc(unsigned long);
extern void free(void *);

#define UNVISITED 2147483647
#define MAXNONBACKPREDS 32768
#define BB_NONHEADER 1
#define BB_REDUCIBLE 2
#define BB_SELF 3
#define BB_IRREDUCIBLE 4
#define BB_DEAD 5

/* the .ls 3-level indexed arrays cap out at 16 x 16 x 32 = 8192 slots */
#define CAP 8192

/* --- growable pointer vector (vec_new/vec_add/vec_at/vec_size) --- */
typedef struct {
    void **items;
    int size;
    int cap;
} Vec;

static void vec_init(Vec *v) {
    v->cap = 4;
    v->size = 0;
    v->items = (void **) malloc(sizeof(void *) * (unsigned long) v->cap);
}

static Vec *vec_new(void) {
    Vec *v = (Vec *) malloc(sizeof(Vec));
    vec_init(v);
    return v;
}

static void vec_add(Vec *v, void *item) {
    if (v->size == v->cap) {
        int ncap = v->cap * 2;
        void **ni = (void **) malloc(sizeof(void *) * (unsigned long) ncap);
        int i;
        for (i = 0; i < v->size; i++) ni[i] = v->items[i];
        free(v->items);
        v->items = ni;
        v->cap = ncap;
    }
    v->items[v->size] = item;
    v->size++;
}

static void *vec_at(Vec *v, int idx) { return v->items[idx]; }
static int vec_size(Vec *v) { return v->size; }

/* --- growable int vector (backPreds entries, iset items) --- */
typedef struct {
    int *items;
    int size;
    int cap;
} IVec;

static void ivec_init(IVec *v) {
    v->cap = 4;
    v->size = 0;
    v->items = (int *) malloc(sizeof(int) * (unsigned long) v->cap);
}

static void ivec_add(IVec *v, int item) {
    if (v->size == v->cap) {
        int ncap = v->cap * 2;
        int *ni = (int *) malloc(sizeof(int) * (unsigned long) ncap);
        int i;
        for (i = 0; i < v->size; i++) ni[i] = v->items[i];
        free(v->items);
        v->items = ni;
        v->cap = ncap;
    }
    v->items[v->size] = item;
    v->size++;
}

/* --- integer set: linear membership scan, exactly like iset_add in the .ls --- */
static int iset_add(IVec *s, int val) {
    int i;
    for (i = 0; i < s->size; i++) {
        if (s->items[i] == val) return 0;
    }
    ivec_add(s, val);
    return 1;
}

/* --- BasicBlock --- */
typedef struct BasicBlock {
    int bid;
    Vec inEdges;   /* of BasicBlock* */
    Vec outEdges;  /* of BasicBlock* */
} BasicBlock;

static BasicBlock *bb_new(int name) {
    BasicBlock *b = (BasicBlock *) malloc(sizeof(BasicBlock));
    b->bid = name;
    vec_init(&b->inEdges);
    vec_init(&b->outEdges);
    return b;
}

/* --- ControlFlowGraph --- */
typedef struct {
    BasicBlock **bbMap;   /* indexed by block name, CAP slots */
    BasicBlock *startNode;
    int numNodes;
} CFG;

static CFG *cfg_new(void) {
    CFG *c = (CFG *) malloc(sizeof(CFG));
    int i;
    c->bbMap = (BasicBlock **) malloc(sizeof(BasicBlock *) * CAP);
    for (i = 0; i < CAP; i++) c->bbMap[i] = 0;
    c->startNode = 0;
    c->numNodes = 0;
    return c;
}

static BasicBlock *cfg_create_node(CFG *cfg, int name) {
    BasicBlock *node = cfg->bbMap[name];
    if (node == 0) {
        node = bb_new(name);
        cfg->bbMap[name] = node;
        cfg->numNodes = cfg->numNodes + 1;
    }
    if (cfg->numNodes == 1) cfg->startNode = node;
    return node;
}

static void cfg_add_edge(CFG *cfg, int fromName, int toName) {
    BasicBlock *fromNode = cfg_create_node(cfg, fromName);
    BasicBlock *toNode = cfg_create_node(cfg, toName);
    vec_add(&fromNode->outEdges, toNode);
    vec_add(&toNode->inEdges, fromNode);
}

static int cfg_get_num_nodes(CFG *cfg) { return cfg->numNodes; }

/* --- big vector with first/size tracking (bvec) --- */
typedef struct {
    void **items;
    int size;
    int cap;
    int first;
} BVec;

static void bvec_init(BVec *v) {
    v->cap = 8;
    v->size = 0;
    v->first = 0;
    v->items = (void **) malloc(sizeof(void *) * (unsigned long) v->cap);
}

static void bvec_add(BVec *v, void *item) {
    if (v->size == v->cap) {
        int ncap = v->cap * 2;
        void **ni = (void **) malloc(sizeof(void *) * (unsigned long) ncap);
        int i;
        for (i = 0; i < v->size; i++) ni[i] = v->items[i];
        free(v->items);
        v->items = ni;
        v->cap = ncap;
    }
    v->items[v->size] = item;
    v->size++;
}

static void *bvec_raw_get(BVec *v, int idx) { return v->items[idx]; }
static int bvec_size(BVec *v) { return v->size - v->first; }

static void *bvec_remove_first(BVec *v) {
    void *r;
    if (v->first >= v->size) return 0;
    r = v->items[v->first];
    v->first = v->first + 1;
    return r;
}

static int bvec_is_empty(BVec *v) { return v->first >= v->size ? 1 : 0; }

/* --- SimpleLoop --- */
typedef struct SimpleLoop {
    int lid;
    int isRed;
    int parentId;
    int isRoot;
    int nestLvl;
    int depthLvl;
    BasicBlock *header;
    BVec bbs;       /* of BasicBlock* */
    BVec children;  /* of SimpleLoop* */
} SimpleLoop;

static SimpleLoop *loop_new(BasicBlock *bb, int isReducible, int counter) {
    SimpleLoop *l = (SimpleLoop *) malloc(sizeof(SimpleLoop));
    l->lid = counter;
    l->isRed = isReducible;
    l->parentId = -1;
    l->isRoot = 0;
    l->nestLvl = 0;
    l->depthLvl = 0;
    l->header = bb;
    bvec_init(&l->bbs);
    bvec_init(&l->children);
    if (bb != 0) bvec_add(&l->bbs, bb);
    return l;
}

static void loop_add_node(SimpleLoop *loop, BasicBlock *bb) { bvec_add(&loop->bbs, bb); }
static void loop_add_child(SimpleLoop *loop, SimpleLoop *child) { bvec_add(&loop->children, child); }

static void loop_set_parent(SimpleLoop *loop, SimpleLoop *parent) {
    loop->parentId = parent->lid;
    loop_add_child(parent, loop);
}

/* --- LoopStructureGraph --- */
typedef struct {
    SimpleLoop **loops;  /* indexed by loop id, CAP slots */
    int loopCounter;
    SimpleLoop *root;
} LSG;

static LSG *lsg_new(void) {
    LSG *l = (LSG *) malloc(sizeof(LSG));
    SimpleLoop *root;
    int i;
    l->loops = (SimpleLoop **) malloc(sizeof(SimpleLoop *) * CAP);
    for (i = 0; i < CAP; i++) l->loops[i] = 0;
    root = loop_new(0, 1, 0);
    root->nestLvl = 0;
    root->isRoot = 1;
    l->loops[0] = root;
    l->loopCounter = 1;
    l->root = root;
    return l;
}

static SimpleLoop *lsg_create_new_loop(LSG *lsg, BasicBlock *bb, int isReducible) {
    int lc = lsg->loopCounter;
    SimpleLoop *loop = loop_new(bb, isReducible, lc);
    lsg->loopCounter = lc + 1;
    lsg->loops[lc] = loop;
    return loop;
}

static int lsg_get_num_loops(LSG *lsg) { return lsg->loopCounter; }

static void lsg_calc_nesting_rec(LSG *lsg, SimpleLoop *loop, int depth) {
    int i;
    loop->depthLvl = depth;
    for (i = loop->children.first; i < loop->children.size; i++) {
        SimpleLoop *child = (SimpleLoop *) bvec_raw_get(&loop->children, i);
        lsg_calc_nesting_rec(lsg, child, depth + 1);
        if (child->nestLvl + 1 > loop->nestLvl) loop->nestLvl = child->nestLvl + 1;
    }
}

static void lsg_calc_nesting(LSG *lsg) {
    int i;
    for (i = 0; i < lsg->loopCounter; i++) {
        SimpleLoop *l = lsg->loops[i];
        if (l != 0) {
            if (l->isRoot == 0) {
                if (l->parentId == -1) loop_set_parent(l, lsg->root);
            }
        }
    }
    lsg_calc_nesting_rec(lsg, lsg->root, 0);
}

/* --- UnionFindNode --- */
typedef struct {
    int dfn;
    int parentDfn;
    BasicBlock *bb;
    SimpleLoop *loop;
} UFNode;

static void uf_init(UFNode *node, BasicBlock *bb, int dfsNum) {
    node->dfn = dfsNum;
    node->parentDfn = dfsNum;
    node->bb = bb;
    node->loop = 0;
}

/* path-compression find, collecting the same compression path as the .ls */
static int uf_find_set(UFNode **nodes, int nodeId) {
    UFNode *node = nodes[nodeId];
    int curp = node->parentDfn;
    UFNode *curNode;
    static UFNode *path[CAP];
    int psz = 0;
    int root;
    int j;
    if (curp == nodeId) return nodeId;
    curNode = node;
    while (nodeId != curp) {
        int pp = curp;
        UFNode *gpNode = nodes[pp];
        int gpp = gpNode->parentDfn;
        if (pp != gpp) {
            path[psz] = curNode;
            psz++;
        }
        nodeId = pp;
        curNode = gpNode;
        curp = gpp;
    }
    root = nodeId;
    for (j = 0; j < psz; j++) path[j]->parentDfn = root;
    return root;
}

/* --- Havlak Loop Finder --- */

static int hlf_is_ancestor(int *hlf_last, int w, int v) {
    if (w > v) return 0;
    if (v > hlf_last[w]) return 0;
    return 1;
}

static int hlf_do_dfs(UFNode **nodes, int *numMap, int *last_arr, BasicBlock *currentBB, int current) {
    int lastId = current;
    int oeSz;
    int i;
    uf_init(nodes[current], currentBB, current);
    numMap[currentBB->bid] = current;
    oeSz = vec_size(&currentBB->outEdges);
    for (i = 0; i < oeSz; i++) {
        BasicBlock *target = (BasicBlock *) vec_at(&currentBB->outEdges, i);
        if (numMap[target->bid] == UNVISITED) {
            lastId = hlf_do_dfs(nodes, numMap, last_arr, target, lastId + 1);
        }
    }
    last_arr[current] = lastId;
    return lastId;
}

static void hlf_process_edges(UFNode **nodes, int *numMap, IVec *backPreds, IVec *nonBackPreds,
                              int *hlf_last, BasicBlock *nodeW, int w) {
    int ieSz = vec_size(&nodeW->inEdges);
    int i;
    for (i = 0; i < ieSz; i++) {
        BasicBlock *nodeV = (BasicBlock *) vec_at(&nodeW->inEdges, i);
        int v = numMap[nodeV->bid];
        if (v != UNVISITED) {
            if (hlf_is_ancestor(hlf_last, w, v) == 1) ivec_add(&backPreds[w], v);
            else iset_add(&nonBackPreds[w], v);
        }
    }
}

static void hlf_step_d(UFNode **nodes, IVec *backPreds, int *hlf_type, int w, BVec *nodePool) {
    IVec *bp = &backPreds[w];
    int i;
    for (i = 0; i < bp->size; i++) {
        int v = bp->items[i];
        if (v != w) {
            int fsId = uf_find_set(nodes, v);
            bvec_add(nodePool, nodes[fsId]);
        } else {
            hlf_type[w] = BB_SELF;
        }
    }
}

/* scan nodePool for an entry whose dfn matches, exactly like bvec_has_dfn */
static int bvec_has_dfn(BVec *v, int id) {
    int i;
    for (i = v->first; i < v->size; i++) {
        UFNode *elem = (UFNode *) v->items[i];
        if (elem != 0) {
            if (elem->dfn == id) return 1;
        }
    }
    return 0;
}

static void hlf_step_e(UFNode **nodes, IVec *nonBackPreds, int *hlf_type, int *hlf_last, int w,
                       BVec *nodePool, BVec *workList, UFNode *x) {
    IVec *nbp = &nonBackPreds[x->dfn];
    int sz = nbp->size;  /* capture size before any growth, as the .ls does */
    int i;
    for (i = 0; i < sz; i++) {
        int iter = nbp->items[i];
        int ydashId = uf_find_set(nodes, iter);
        UFNode *ydash = nodes[ydashId];
        int yddfn = ydash->dfn;
        if (hlf_is_ancestor(hlf_last, w, yddfn) == 0) {
            hlf_type[w] = BB_IRREDUCIBLE;
            iset_add(&nonBackPreds[w], yddfn);
        } else {
            if (yddfn != w) {
                if (bvec_has_dfn(nodePool, yddfn) == 0) {
                    bvec_add(workList, ydash);
                    bvec_add(nodePool, ydash);
                }
            }
        }
    }
}

static void hlf_set_loop_attrs(UFNode **nodes, int *hlf_header, int w, BVec *nodePool, SimpleLoop *loop) {
    int i;
    nodes[w]->loop = loop;
    for (i = nodePool->first; i < nodePool->size; i++) {
        UFNode *node = (UFNode *) bvec_raw_get(nodePool, i);
        hlf_header[node->dfn] = w;
        node->parentDfn = w;
        if (node->loop != 0) {
            loop_set_parent(node->loop, loop);
        } else {
            if (node->bb != 0) loop_add_node(loop, node->bb);
        }
    }
}

static int hlf_find_loops(CFG *cfg, LSG *lsg) {
    int size;
    IVec *nonBackPreds;
    IVec *backPreds;
    int *numMap;
    int *hlf_header;
    int *hlf_type;
    int *hlf_last;
    UFNode **nodes;
    int maxBid;
    int mi;
    int ni;
    int wi;
    int w;
    if (cfg->startNode == 0) return 0;
    size = cfg_get_num_nodes(cfg);

    nonBackPreds = (IVec *) malloc(sizeof(IVec) * (unsigned long) size);
    backPreds = (IVec *) malloc(sizeof(IVec) * (unsigned long) size);
    numMap = (int *) malloc(sizeof(int) * CAP);
    hlf_header = (int *) malloc(sizeof(int) * CAP);
    hlf_type = (int *) malloc(sizeof(int) * CAP);
    hlf_last = (int *) malloc(sizeof(int) * CAP);
    nodes = (UFNode **) malloc(sizeof(UFNode *) * (unsigned long) size);

    /* the .ls integer arrays default every slot to 0 */
    for (mi = 0; mi < CAP; mi++) {
        numMap[mi] = 0;
        hlf_header[mi] = 0;
        hlf_type[mi] = 0;
        hlf_last[mi] = 0;
    }

    /* initialize numMap to UNVISITED */
    maxBid = size + 100;
    for (mi = 0; mi < maxBid; mi++) numMap[mi] = UNVISITED;

    /* create UF nodes and per-node structures */
    for (ni = 0; ni < size; ni++) {
        UFNode *ufn = (UFNode *) malloc(sizeof(UFNode));
        ufn->dfn = 0;
        ufn->parentDfn = 0;
        ufn->bb = 0;
        ufn->loop = 0;
        nodes[ni] = ufn;
        ivec_init(&nonBackPreds[ni]);
        ivec_init(&backPreds[ni]);
    }

    /* DFS */
    hlf_do_dfs(nodes, numMap, hlf_last, cfg->startNode, 0);

    /* identify edges */
    for (wi = 0; wi < size; wi++) {
        hlf_header[wi] = 0;
        hlf_type[wi] = BB_NONHEADER;
        if (nodes[wi]->bb == 0) {
            hlf_type[wi] = BB_DEAD;
        } else {
            hlf_process_edges(nodes, numMap, backPreds, nonBackPreds, hlf_last, nodes[wi]->bb, wi);
        }
    }

    hlf_header[0] = 0;

    /* step c: process in reverse DFS order */
    for (w = size - 1; w >= 0; w--) {
        BVec nodePool;
        BasicBlock *nodeW;
        bvec_init(&nodePool);
        nodeW = nodes[w]->bb;
        if (nodeW != 0) {
            BVec workList;
            int cpi;
            int npSz2;
            int wtype;
            hlf_step_d(nodes, backPreds, hlf_type, w, &nodePool);
            bvec_init(&workList);
            for (cpi = nodePool.first; cpi < nodePool.size; cpi++) {
                bvec_add(&workList, bvec_raw_get(&nodePool, cpi));
            }
            if (bvec_size(&nodePool) != 0) hlf_type[w] = BB_REDUCIBLE;
            while (bvec_is_empty(&workList) == 0) {
                UFNode *x = (UFNode *) bvec_remove_first(&workList);
                if (nonBackPreds[x->dfn].size > MAXNONBACKPREDS) {
                    free(workList.items);
                    free(nodePool.items);
                    return 0;
                }
                hlf_step_e(nodes, nonBackPreds, hlf_type, hlf_last, w, &nodePool, &workList, x);
            }
            npSz2 = bvec_size(&nodePool);
            wtype = hlf_type[w];
            if (npSz2 > 0) {
                int isRed = 1;
                SimpleLoop *loop;
                if (wtype == BB_IRREDUCIBLE) isRed = 0;
                loop = lsg_create_new_loop(lsg, nodeW, isRed);
                hlf_set_loop_attrs(nodes, hlf_header, w, &nodePool, loop);
            } else {
                if (wtype == BB_SELF) {
                    SimpleLoop *loop2 = lsg_create_new_loop(lsg, nodeW, 1);
                    hlf_set_loop_attrs(nodes, hlf_header, w, &nodePool, loop2);
                }
            }
            free(workList.items);
        }
        free(nodePool.items);
    }

    for (ni = 0; ni < size; ni++) {
        free(nonBackPreds[ni].items);
        free(backPreds[ni].items);
    }
    free(nonBackPreds);
    free(backPreds);
    free(numMap);
    free(hlf_header);
    free(hlf_type);
    free(hlf_last);
    /* UF nodes may be referenced by loops via nothing (loops hold bbs, not UF
     * nodes), so they are safe to release here */
    for (ni = 0; ni < size; ni++) free(nodes[ni]);
    free(nodes);
    return 1;
}

/* --- LoopTesterApp --- */

static int build_diamond(CFG *cfg, int start) {
    int bb0 = start;
    cfg_add_edge(cfg, bb0, bb0 + 1);
    cfg_add_edge(cfg, bb0, bb0 + 2);
    cfg_add_edge(cfg, bb0 + 1, bb0 + 3);
    cfg_add_edge(cfg, bb0 + 2, bb0 + 3);
    return bb0 + 3;
}

static void build_connect(CFG *cfg, int start, int end) {
    cfg_add_edge(cfg, start, end);
}

static int build_straight(CFG *cfg, int start, int n) {
    int i;
    for (i = 0; i < n; i++) build_connect(cfg, start + i, start + i + 1);
    return start + n;
}

static int build_base_loop(CFG *cfg, int from) {
    int header = build_straight(cfg, from, 1);
    int diamond1 = build_diamond(cfg, header);
    int d11 = build_straight(cfg, diamond1, 1);
    int diamond2 = build_diamond(cfg, d11);
    int footer = build_straight(cfg, diamond2, 1);
    build_connect(cfg, diamond2, d11);
    build_connect(cfg, diamond1, header);
    build_connect(cfg, footer, from);
    footer = build_straight(cfg, footer, 1);
    return footer;
}

static void construct_simple_cfg(CFG *cfg) {
    cfg_create_node(cfg, 0);
    build_base_loop(cfg, 0);
    cfg_create_node(cfg, 1);
    cfg_add_edge(cfg, 0, 2);
}

static void find_loops(CFG *cfg, LSG *lsg) {
    hlf_find_loops(cfg, lsg);
}

static void add_dummy_loops(CFG *cfg, LSG *lsg, int numDummyLoops) {
    int i;
    for (i = 0; i < numDummyLoops; i++) find_loops(cfg, lsg);
}

static void construct_cfg(CFG *cfg, int parLoops, int pparLoops, int ppparLoops) {
    int n = 2;
    int pl;
    for (pl = 0; pl < parLoops; pl++) {
        int i;
        cfg_create_node(cfg, n + 1);
        build_connect(cfg, 2, n + 1);
        n = n + 1;
        for (i = 0; i < pparLoops; i++) {
            int top = n;
            int bottom;
            int j;
            n = build_straight(cfg, n, 1);
            for (j = 0; j < ppparLoops; j++) n = build_base_loop(cfg, n);
            bottom = build_straight(cfg, n, 1);
            build_connect(cfg, n, top);
            n = bottom;
        }
        build_connect(cfg, n, 1);
    }
}

static int lta_main(int numDummyLoops, int findLoopIterations, int parLoops, int pparLoops, int ppparLoops) {
    CFG *cfg = cfg_new();
    LSG *lsg;
    int i;
    int numLoops;
    int numNodes;
    cfg_create_node(cfg, 0);
    construct_simple_cfg(cfg);
    lsg = lsg_new();
    add_dummy_loops(cfg, lsg, numDummyLoops);
    construct_cfg(cfg, parLoops, pparLoops, ppparLoops);
    find_loops(cfg, lsg);
    for (i = 0; i < findLoopIterations; i++) {
        LSG *newLsg = lsg_new();
        find_loops(cfg, newLsg);
    }
    lsg_calc_nesting(lsg);
    numLoops = lsg_get_num_loops(lsg);
    numNodes = cfg_get_num_nodes(cfg);
    /* pack both into a single int: loops * 100000 + nodes */
    return numLoops * 100000 + numNodes;
}

static int verify_result(int result, int innerIterations) {
    int remainder = result % 100000;
    int lcount = result / 100000;
    if (innerIterations == 1) {
        if (lcount == 1605 && remainder == 5213) return 1;
    }
    if (innerIterations == 15) {
        if (lcount == 1647 && remainder == 5213) return 1;
    }
    printf("Unexpected: loops=%d nodes=%d iters=%d\n", lcount, remainder, innerIterations);
    return 0;
}

int main(void) {
    int result = lta_main(1, 1, 10, 10, 5);
    int ok = verify_result(result, 1);
    printf(ok == 1 ? "Havlak: PASS\n" : "Havlak: FAIL\n");
    return ok != 1;
}
