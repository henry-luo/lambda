/* Native C2MIR port of awfy/cd.ls. */
extern int printf(const char *, ...);
extern void *malloc(unsigned long);
extern void free(void *);
extern double sqrt(double);
extern double sin(double);
extern double cos(double);

#define MIN_X 0.0
#define MIN_Y 0.0
#define MAX_X 1000.0
#define MAX_Y 1000.0
#define MIN_Z 0.0
#define MAX_Z 10.0
#define PROXIMITY_RADIUS 1.0
#define GOOD_VOXEL_SIZE 2.0
#define RED 1
#define BLACK 0
#define NIL (-1)

#define NUM_AIRCRAFT 100
#define NUM_FRAMES 200

/* --- helpers --- */
static double safe_div(double a, double b) {
    if (b == 0.0) return 0.0;
    return a / b;
}

static double min_f(double a, double b) { return a <= b ? a : b; }
static double max_f(double a, double b) { return a >= b ? a : b; }

static int check_overlap(double low, double high) {
    if (low <= 1.0) {
        if (1.0 <= high) return 1;
    }
    if (low <= 0.0) {
        if (0.0 <= high) return 1;
    }
    if (0.0 <= low) {
        if (high <= 1.0) return 1;
    }
    return 0;
}

/* --- growable int vector (voxel motion-index lists) --- */
typedef struct {
    int *items;
    int size;
    int cap;
} IVec;

static IVec *ivec_new(void) {
    IVec *v = (IVec *) malloc(sizeof(IVec));
    v->cap = 4;
    v->size = 0;
    v->items = (int *) malloc(sizeof(int) * (unsigned long) v->cap);
    return v;
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

/* =====================================================
 * Red-Black Tree (integer keys), id-based node storage,
 * ported from the .ls node-array layout [key,val,l,r,p,c]
 * ===================================================== */
typedef struct {
    int key;
    void *val;
    int l;
    int r;
    int p;
    int c;
} RBNode;

typedef struct {
    RBNode *nd;
    int cap;
    int cnt;
    int root;
} RBT;

static RBT *rbt_new(void) {
    RBT *t = (RBT *) malloc(sizeof(RBT));
    t->cap = 16;
    t->cnt = 0;
    t->root = NIL;
    t->nd = (RBNode *) malloc(sizeof(RBNode) * (unsigned long) t->cap);
    return t;
}

static void rbt_free(RBT *t) {
    free(t->nd);
    free(t);
}

static int rbt_mk_node(RBT *t, int key, void *val) {
    int c = t->cnt;
    RBNode *n;
    if (c == t->cap) {
        int ncap = t->cap * 2;
        RBNode *nn = (RBNode *) malloc(sizeof(RBNode) * (unsigned long) ncap);
        int i;
        for (i = 0; i < c; i++) nn[i] = t->nd[i];
        free(t->nd);
        t->nd = nn;
        t->cap = ncap;
    }
    n = &t->nd[c];
    n->key = key;
    n->val = val;
    n->l = NIL;
    n->r = NIL;
    n->p = NIL;
    n->c = RED;
    t->cnt = c + 1;
    return c;
}

static int rbt_left_rotate(RBT *t, int xId) {
    RBNode *xn = &t->nd[xId];
    int yId = xn->r;
    RBNode *yn = &t->nd[yId];
    int ylId = yn->l;
    int xpId;
    /* x.right = y.left */
    xn->r = ylId;
    if (ylId != NIL) t->nd[ylId].p = xId;
    /* y.parent = x.parent */
    xpId = xn->p;
    yn->p = xpId;
    if (xpId == NIL) {
        t->root = yId;
    } else {
        RBNode *xpn = &t->nd[xpId];
        if (xId == xpn->l) xpn->l = yId;
        else xpn->r = yId;
    }
    /* y.left = x */
    yn->l = xId;
    xn->p = yId;
    return yId;
}

static int rbt_right_rotate(RBT *t, int yId) {
    RBNode *yn = &t->nd[yId];
    int xId = yn->l;
    RBNode *xn = &t->nd[xId];
    int xrId = xn->r;
    int ypId;
    /* y.left = x.right */
    yn->l = xrId;
    if (xrId != NIL) t->nd[xrId].p = yId;
    /* x.parent = y.parent */
    ypId = yn->p;
    xn->p = ypId;
    if (ypId == NIL) {
        t->root = xId;
    } else {
        RBNode *ypn = &t->nd[ypId];
        if (yId == ypn->l) ypn->l = xId;
        else ypn->r = xId;
    }
    /* x.right = y */
    xn->r = yId;
    yn->p = xId;
    return xId;
}

static void *rbt_put(RBT *t, int key, void *value) {
    int yId = NIL;
    int xId = t->root;
    int zId;
    int curId;
    int rootId;
    while (xId != NIL) {
        RBNode *xn = &t->nd[xId];
        int xk = xn->key;
        yId = xId;
        if (key < xk) {
            xId = xn->l;
        } else if (key > xk) {
            xId = xn->r;
        } else {
            void *oldVal = xn->val;
            xn->val = value;
            return oldVal;
        }
    }
    zId = rbt_mk_node(t, key, value);
    t->nd[zId].p = yId;
    if (yId == NIL) {
        t->root = zId;
    } else {
        RBNode *yn = &t->nd[yId];
        if (key < yn->key) yn->l = zId;
        else yn->r = zId;
    }
    /* fix up */
    curId = zId;
    rootId = t->root;
    while (curId != rootId) {
        RBNode *cur = &t->nd[curId];
        int pId = cur->p;
        RBNode *parn = &t->nd[pId];
        int pcol = parn->c;
        if (pcol != RED) {
            curId = rootId;
        } else {
            int ppId = parn->p;
            RBNode *ppn = &t->nd[ppId];
            int ppLId = ppn->l;
            if (pId == ppLId) {
                int uncId = ppn->r;
                int uncCol = BLACK;
                if (uncId != NIL) uncCol = t->nd[uncId].c;
                if (uncCol == RED) {
                    parn->c = BLACK;
                    t->nd[uncId].c = BLACK;
                    ppn->c = RED;
                    curId = ppId;
                    rootId = t->root;
                } else {
                    int crpId;
                    RBNode *p3;
                    int p3Id;
                    int pp3Id;
                    crpId = t->nd[curId].p;
                    if (curId == t->nd[crpId].r) {
                        curId = pId;
                        rbt_left_rotate(t, curId);
                    }
                    p3Id = t->nd[curId].p;
                    p3 = &t->nd[p3Id];
                    p3->c = BLACK;
                    pp3Id = p3->p;
                    t->nd[pp3Id].c = RED;
                    rbt_right_rotate(t, pp3Id);
                    rootId = t->root;
                }
            } else {
                int uncId2 = ppn->l;
                int uncCol2 = BLACK;
                if (uncId2 != NIL) uncCol2 = t->nd[uncId2].c;
                if (uncCol2 == RED) {
                    parn->c = BLACK;
                    t->nd[uncId2].c = BLACK;
                    ppn->c = RED;
                    curId = ppId;
                    rootId = t->root;
                } else {
                    int c2pId;
                    RBNode *p4;
                    int p4Id;
                    int pp4Id;
                    c2pId = t->nd[curId].p;
                    if (curId == t->nd[c2pId].l) {
                        curId = pId;
                        rbt_right_rotate(t, curId);
                    }
                    p4Id = t->nd[curId].p;
                    p4 = &t->nd[p4Id];
                    p4->c = BLACK;
                    pp4Id = p4->p;
                    t->nd[pp4Id].c = RED;
                    rbt_left_rotate(t, pp4Id);
                    rootId = t->root;
                }
            }
        }
    }
    t->nd[t->root].c = BLACK;
    return 0;
}

static int rbt_find_node(RBT *t, int key) {
    int curId = t->root;
    while (curId != NIL) {
        RBNode *n = &t->nd[curId];
        int nk = n->key;
        if (key == nk) return curId;
        if (key < nk) curId = n->l;
        else curId = n->r;
    }
    return NIL;
}

static void *rbt_get(RBT *t, int key) {
    int nId = rbt_find_node(t, key);
    if (nId == NIL) return 0;
    return t->nd[nId].val;
}

static int rbt_tree_min(RBT *t, int xId) {
    while (xId != NIL) {
        int lId = t->nd[xId].l;
        if (lId == NIL) return xId;
        xId = lId;
    }
    return xId;
}

static int rbt_successor(RBT *t, int xId) {
    RBNode *xn = &t->nd[xId];
    int rId = xn->r;
    int yId;
    if (rId != NIL) return rbt_tree_min(t, rId);
    yId = xn->p;
    while (yId != NIL) {
        RBNode *yn = &t->nd[yId];
        if (xId != yn->r) return yId;
        xId = yId;
        yId = yn->p;
    }
    return NIL;
}

static int rbt_first(RBT *t) {
    if (t->root == NIL) return NIL;
    return rbt_tree_min(t, t->root);
}

static void rbt_remove_fixup(RBT *t, int xId, int xParId) {
    int rootId = t->root;
    while (xId != rootId) {
        int xCol = BLACK;
        if (xId != NIL) xCol = t->nd[xId].c;
        if (xCol != BLACK) {
            xId = rootId;
        } else {
            RBNode *xpn = &t->nd[xParId];
            int xplId = xpn->l;
            if (xId == xplId) {
                int wId = xpn->r;
                RBNode *wn = &t->nd[wId];
                int wc = wn->c;
                int wlc;
                int wlId;
                int wrc;
                int wrId;
                int didCase2;
                if (wc == RED) {
                    wn->c = BLACK;
                    xpn->c = RED;
                    rbt_left_rotate(t, xParId);
                    xpn = &t->nd[xParId];
                    wId = xpn->r;
                    wn = &t->nd[wId];
                }
                wlc = BLACK;
                wlId = wn->l;
                if (wlId != NIL) wlc = t->nd[wlId].c;
                wrc = BLACK;
                wrId = wn->r;
                if (wrId != NIL) wrc = t->nd[wrId].c;
                didCase2 = 0;
                if (wlc == BLACK) {
                    if (wrc == BLACK) {
                        wn->c = RED;
                        xId = xParId;
                        xParId = t->nd[xId].p;
                        rootId = t->root;
                        didCase2 = 1;
                    }
                }
                if (didCase2 == 0) {
                    /* refresh */
                    wn = &t->nd[wId];
                    wrId = wn->r;
                    wrc = BLACK;
                    if (wrId != NIL) wrc = t->nd[wrId].c;
                    if (wrc == BLACK) {
                        wlId = wn->l;
                        if (wlId != NIL) t->nd[wlId].c = BLACK;
                        wn->c = RED;
                        rbt_right_rotate(t, wId);
                        xpn = &t->nd[xParId];
                        wId = xpn->r;
                        wn = &t->nd[wId];
                    }
                    wn->c = xpn->c;
                    xpn->c = BLACK;
                    wrId = wn->r;
                    if (wrId != NIL) t->nd[wrId].c = BLACK;
                    rbt_left_rotate(t, xParId);
                    xId = t->root;
                    rootId = xId;
                    xParId = t->nd[xId].p;
                }
            } else {
                int wId2 = xpn->l;
                RBNode *wn2 = &t->nd[wId2];
                int wc2 = wn2->c;
                int wrc2;
                int wrId2;
                int wlc2;
                int wlId2;
                int didCase2b;
                if (wc2 == RED) {
                    wn2->c = BLACK;
                    xpn->c = RED;
                    rbt_right_rotate(t, xParId);
                    xpn = &t->nd[xParId];
                    wId2 = xpn->l;
                    wn2 = &t->nd[wId2];
                }
                wrc2 = BLACK;
                wrId2 = wn2->r;
                if (wrId2 != NIL) wrc2 = t->nd[wrId2].c;
                wlc2 = BLACK;
                wlId2 = wn2->l;
                if (wlId2 != NIL) wlc2 = t->nd[wlId2].c;
                didCase2b = 0;
                if (wrc2 == BLACK) {
                    if (wlc2 == BLACK) {
                        wn2->c = RED;
                        xId = xParId;
                        xParId = t->nd[xId].p;
                        rootId = t->root;
                        didCase2b = 1;
                    }
                }
                if (didCase2b == 0) {
                    wlId2 = wn2->l;
                    wlc2 = BLACK;
                    if (wlId2 != NIL) wlc2 = t->nd[wlId2].c;
                    if (wlc2 == BLACK) {
                        wrId2 = wn2->r;
                        if (wrId2 != NIL) t->nd[wrId2].c = BLACK;
                        wn2->c = RED;
                        rbt_left_rotate(t, wId2);
                        xpn = &t->nd[xParId];
                        wId2 = xpn->l;
                        wn2 = &t->nd[wId2];
                    }
                    wn2->c = xpn->c;
                    xpn->c = BLACK;
                    wlId2 = wn2->l;
                    if (wlId2 != NIL) t->nd[wlId2].c = BLACK;
                    rbt_right_rotate(t, xParId);
                    xId = t->root;
                    rootId = xId;
                    xParId = t->nd[xId].p;
                }
            }
        }
    }
    if (xId != NIL) t->nd[xId].c = BLACK;
}

static void *rbt_remove(RBT *t, int key) {
    int zId = rbt_find_node(t, key);
    RBNode *zn;
    void *zv;
    int yId;
    RBNode *yn;
    int ylId;
    int yrId;
    int xId;
    int xParId;
    int ypId;
    if (zId == NIL) return 0;
    zn = &t->nd[zId];
    zv = zn->val;
    yId = zId;
    if (zn->l != NIL) {
        if (zn->r != NIL) yId = rbt_successor(t, zId);
    }
    yn = &t->nd[yId];
    ylId = yn->l;
    yrId = yn->r;
    if (ylId != NIL) xId = ylId;
    else xId = yrId;
    xParId = yn->p;
    if (xId != NIL) t->nd[xId].p = xParId;
    ypId = yn->p;
    if (ypId == NIL) {
        t->root = xId;
    } else {
        RBNode *ypn = &t->nd[ypId];
        if (yId == ypn->l) ypn->l = xId;
        else ypn->r = xId;
    }
    if (yId != zId) {
        int znlId;
        int znrId;
        int zpId;
        if (yn->c == BLACK) rbt_remove_fixup(t, xId, xParId);
        yn->p = zn->p;
        yn->c = zn->c;
        yn->l = zn->l;
        yn->r = zn->r;
        znlId = zn->l;
        if (znlId != NIL) t->nd[znlId].p = yId;
        znrId = zn->r;
        if (znrId != NIL) t->nd[znrId].p = yId;
        zpId = zn->p;
        if (zpId != NIL) {
            RBNode *zpn = &t->nd[zpId];
            if (zId == zpn->l) zpn->l = yId;
            else zpn->r = yId;
        } else {
            t->root = yId;
        }
    } else {
        if (yn->c == BLACK) rbt_remove_fixup(t, xId, xParId);
    }
    return zv;
}

/* --- Vector2D key encoding --- */
static int v2d_key(double x, double y) {
    int kx = (int) x + 1000;
    int ky = (int) y + 1000;
    return kx * 100000 + ky;
}

/* --- voxel hashing --- */
static void voxel_hash_xy(double px, double py, double out[2]) {
    int xdiv = (int) (px / GOOD_VOXEL_SIZE);
    int ydiv = (int) (py / GOOD_VOXEL_SIZE);
    double rx = GOOD_VOXEL_SIZE * (double) xdiv;
    double ry = GOOD_VOXEL_SIZE * (double) ydiv;
    if (px < 0.0) rx = rx - GOOD_VOXEL_SIZE;
    if (py < 0.0) ry = ry - GOOD_VOXEL_SIZE;
    out[0] = rx;
    out[1] = ry;
}

/* --- isInVoxel check --- */
static int is_in_voxel(double vx, double vy, double p1x, double p1y, double p2x, double p2y) {
    double vS;
    double r;
    double x0;
    double xv;
    double y0;
    double yv;
    double rawLX;
    double rawHX;
    double lowX;
    double highX;
    double rawLY;
    double rawHY;
    double lowY;
    double highY;
    int xOk;
    int yOk;
    if (vx > MAX_X) return 0;
    if (vx < MIN_X) return 0;
    if (vy > MAX_Y) return 0;
    if (vy < MIN_Y) return 0;

    vS = GOOD_VOXEL_SIZE;
    r = PROXIMITY_RADIUS / 2.0;
    x0 = p1x;
    xv = p2x - p1x;
    y0 = p1y;
    yv = p2y - p1y;

    rawLX = safe_div(vx - r - x0, xv);
    rawHX = safe_div(vx + vS + r - x0, xv);
    lowX = min_f(rawLX, rawHX);
    highX = max_f(rawLX, rawHX);

    xOk = 0;
    if (xv == 0.0) {
        if (vx <= x0 + r) {
            if (x0 - r <= vx + vS) xOk = 1;
        }
    } else {
        xOk = check_overlap(lowX, highX);
    }
    if (xOk == 0) return 0;

    rawLY = safe_div(vy - r - y0, yv);
    rawHY = safe_div(vy + vS + r - y0, yv);
    lowY = min_f(rawLY, rawHY);
    highY = max_f(rawLY, rawHY);

    yOk = 0;
    if (yv == 0.0) {
        if (vy <= y0 + r) {
            if (y0 - r <= vy + vS) yOk = 1;
        }
    } else {
        yOk = check_overlap(lowY, highY);
    }
    if (yOk == 0) return 0;

    if (xv == 0.0) return 1;
    if (yv == 0.0) return 1;
    if (lowY <= highX) {
        if (highX <= highY) return 1;
    }
    if (lowY <= lowX) {
        if (lowX <= highY) return 1;
    }
    if (lowX <= lowY) {
        if (highY <= highX) return 1;
    }
    return 0;
}

/* --- recurse: draw motion into voxel map --- */
static void recurse_draw(RBT *voxelMap, RBT *seenTree, double vx, double vy,
                         double p1x, double p1y, double p2x, double p2y, int motionIdx) {
    int vk;
    void *oldSeen;
    IVec *existVec;
    double gs;
    if (is_in_voxel(vx, vy, p1x, p1y, p2x, p2y) == 0) return;

    vk = v2d_key(vx, vy);
    oldSeen = rbt_put(seenTree, vk, (void *) 1);
    if (oldSeen != 0) return;

    existVec = (IVec *) rbt_get(voxelMap, vk);
    if (existVec == 0) existVec = ivec_new();
    ivec_add(existVec, motionIdx);
    rbt_put(voxelMap, vk, existVec);

    gs = GOOD_VOXEL_SIZE;
    recurse_draw(voxelMap, seenTree, vx - gs, vy, p1x, p1y, p2x, p2y, motionIdx);
    recurse_draw(voxelMap, seenTree, vx + gs, vy, p1x, p1y, p2x, p2y, motionIdx);
    recurse_draw(voxelMap, seenTree, vx, vy - gs, p1x, p1y, p2x, p2y, motionIdx);
    recurse_draw(voxelMap, seenTree, vx, vy + gs, p1x, p1y, p2x, p2y, motionIdx);
    recurse_draw(voxelMap, seenTree, vx - gs, vy - gs, p1x, p1y, p2x, p2y, motionIdx);
    recurse_draw(voxelMap, seenTree, vx - gs, vy + gs, p1x, p1y, p2x, p2y, motionIdx);
    recurse_draw(voxelMap, seenTree, vx + gs, vy - gs, p1x, p1y, p2x, p2y, motionIdx);
    recurse_draw(voxelMap, seenTree, vx + gs, vy + gs, p1x, p1y, p2x, p2y, motionIdx);
}

/* --- Motion: cs + start/end positions --- */
typedef struct {
    int cs;
    double p1x, p1y, p1z;
    double p2x, p2y, p2z;
} Motion;

/* --- findIntersection between two motions; returns 1 when they collide --- */
static int find_intersection(Motion *m1, Motion *m2) {
    double i1x = m1->p1x;
    double i1y = m1->p1y;
    double i1z = m1->p1z;
    double i2x = m2->p1x;
    double i2y = m2->p1y;
    double i2z = m2->p1z;

    double v1x = m1->p2x - i1x;
    double v1y = m1->p2y - i1y;
    double v1z = m1->p2z - i1z;
    double v2x = m2->p2x - i2x;
    double v2y = m2->p2y - i2y;
    double v2z = m2->p2z - i2z;

    double radius = PROXIMITY_RADIUS;
    double dvx = v2x - v1x;
    double dvy = v2y - v1y;
    double dvz = v2z - v1z;
    double a = dvx * dvx + dvy * dvy + dvz * dvz;

    double pdx;
    double pdy;
    double pdz;
    double dist;

    if (a != 0.0) {
        double dix = i1x - i2x;
        double diy = i1y - i2y;
        double diz = i1z - i2z;
        double dmvx = v1x - v2x;
        double dmvy = v1y - v2y;
        double dmvz = v1z - v2z;
        double b = 2.0 * (dix * dmvx + diy * dmvy + diz * dmvz);

        double di2x = i2x - i1x;
        double di2y = i2y - i1y;
        double di2z = i2z - i1z;
        double c = 0.0 - radius * radius + di2x * di2x + di2y * di2y + di2z * di2z;

        double discr = b * b - 4.0 * a * c;
        double sq;
        double a2;
        double t1;
        double t2;
        if (discr < 0.0) return 0;

        sq = sqrt(discr);
        a2 = 2.0 * a;
        t1 = (-b - sq) / a2;
        t2 = (-b + sq) / a2;

        if (t1 <= t2) {
            int collision = 0;
            if (t1 <= 1.0) {
                if (1.0 <= t2) collision = 1;
            }
            if (t1 <= 0.0) {
                if (0.0 <= t2) collision = 1;
            }
            if (0.0 <= t1) {
                if (t2 <= 1.0) collision = 1;
            }
            if (collision == 1) {
                double v = max_f(t1, 0.0);
                double r1x = i1x + v1x * v;
                double r1y = i1y + v1y * v;
                double r1z = i1z + v1z * v;
                double r2x = i2x + v2x * v;
                double r2y = i2y + v2y * v;
                double r2z = i2z + v2z * v;
                double rx = (r1x + r2x) * 0.5;
                double ry = (r1y + r2y) * 0.5;
                double rz = (r1z + r2z) * 0.5;
                if (rx >= MIN_X) {
                    if (rx <= MAX_X) {
                        if (ry >= MIN_Y) {
                            if (ry <= MAX_Y) {
                                if (rz >= MIN_Z) {
                                    if (rz <= MAX_Z) return 1;
                                }
                            }
                        }
                    }
                }
            }
        }
        return 0;
    }

    /* parallel case */
    pdx = i2x - i1x;
    pdy = i2y - i1y;
    pdz = i2z - i1z;
    dist = sqrt(pdx * pdx + pdy * pdy + pdz * pdz);
    if (dist <= radius) return 1;
    return 0;
}

/* --- CD benchmark main logic --- */
typedef struct {
    int cs;
    double x, y, z;
} Aircraft;

static int simulate_frame(int numAircraft, double tval, Aircraft frame[NUM_AIRCRAFT]) {
    int size = 0;
    int i = 0;
    while (i < numAircraft) {
        frame[size].cs = i;
        frame[size].x = tval;
        frame[size].y = cos(tval) * 2.0 + (double) i * 3.0;
        frame[size].z = 10.0;
        size++;
        frame[size].cs = i + 1;
        frame[size].x = tval;
        frame[size].y = sin(tval) * 2.0 + (double) i * 3.0;
        frame[size].z = 10.0;
        size++;
        i = i + 2;
    }
    return size;
}

static int handle_new_frame(RBT *stateTree, Aircraft *frame, int frameSz) {
    Motion motions[NUM_AIRCRAFT];
    int motionsSz = 0;
    RBT *seenTree = rbt_new();
    RBT *voxelMap;
    int toRemove[NUM_AIRCRAFT];
    int trSz = 0;
    int i;
    int curId;
    int ri;
    int mi;
    double vxy[2];
    int collisionCount = 0;
    int vmCur;

    for (i = 0; i < frameSz; i++) {
        Aircraft *aircraft = &frame[i];
        int csId = aircraft->cs;
        double npx = aircraft->x;
        double npy = aircraft->y;
        double npz = aircraft->z;
        double *newPos = (double *) malloc(sizeof(double) * 3);
        double *oldPos;
        double *usePos;
        Motion *m;
        newPos[0] = npx;
        newPos[1] = npy;
        newPos[2] = npz;
        oldPos = (double *) rbt_put(stateTree, csId, newPos);
        rbt_put(seenTree, csId, (void *) 1);
        usePos = oldPos == 0 ? newPos : oldPos;
        m = &motions[motionsSz];
        m->cs = csId;
        m->p1x = usePos[0];
        m->p1y = usePos[1];
        m->p1z = usePos[2];
        m->p2x = npx;
        m->p2y = npy;
        m->p2z = npz;
        motionsSz++;
        if (oldPos != 0) free(oldPos);
    }

    /* remove aircraft no longer present */
    curId = rbt_first(stateTree);
    while (curId != NIL) {
        int ck = stateTree->nd[curId].key;
        if (rbt_get(seenTree, ck) == 0) {
            toRemove[trSz] = ck;
            trSz++;
        }
        curId = rbt_successor(stateTree, curId);
    }
    for (ri = 0; ri < trSz; ri++) {
        double *pos = (double *) rbt_remove(stateTree, toRemove[ri]);
        if (pos != 0) free(pos);
    }
    rbt_free(seenTree);

    /* reduce collision set */
    voxelMap = rbt_new();
    for (mi = 0; mi < motionsSz; mi++) {
        Motion *mot = &motions[mi];
        RBT *motSeen;
        voxel_hash_xy(mot->p1x, mot->p1y, vxy);
        motSeen = rbt_new();
        recurse_draw(voxelMap, motSeen, vxy[0], vxy[1], mot->p1x, mot->p1y, mot->p2x, mot->p2y, mi);
        rbt_free(motSeen);
    }

    /* collect voxels with >1 motion and check collisions */
    vmCur = rbt_first(voxelMap);
    while (vmCur != NIL) {
        IVec *motVec = (IVec *) voxelMap->nd[vmCur].val;
        int mvsz = motVec->size;
        if (mvsz > 1) {
            int ii;
            for (ii = 0; ii < mvsz; ii++) {
                Motion *mot1 = &motions[motVec->items[ii]];
                int jj;
                for (jj = ii + 1; jj < mvsz; jj++) {
                    Motion *mot2 = &motions[motVec->items[jj]];
                    if (find_intersection(mot1, mot2) != 0) collisionCount++;
                }
            }
        }
        vmCur = rbt_successor(voxelMap, vmCur);
    }

    /* release the per-frame voxel map together with its motion-index lists */
    for (mi = 0; mi < voxelMap->cnt; mi++) {
        IVec *v = (IVec *) voxelMap->nd[mi].val;
        free(v->items);
        free(v);
    }
    rbt_free(voxelMap);

    return collisionCount;
}

static int cd(int numAircraft) {
    RBT *stateTree = rbt_new();
    int actualCollisions = 0;
    int i;
    Aircraft frame[NUM_AIRCRAFT];
    for (i = 0; i < NUM_FRAMES; i++) {
        double tval = (double) i / 10.0;
        int frameSz = simulate_frame(numAircraft, tval, frame);
        actualCollisions += handle_new_frame(stateTree, frame, frameSz);
    }
    return actualCollisions;
}

static int verify_result(int collisions, int numAircraft) {
    if (numAircraft == 100) {
        if (collisions == 4305) return 1;
    }
    if (numAircraft == 10) {
        if (collisions == 390) return 1;
    }
    if (numAircraft == 2) {
        if (collisions == 42) return 1;
    }
    printf("Unexpected: collisions=%d aircraft=%d\n", collisions, numAircraft);
    return 0;
}

int main(void) {
    int collisions = cd(100);
    int ok;
    printf("collisions=%d\n", collisions);
    ok = verify_result(collisions, 100);
    printf(ok == 1 ? "CD: PASS\n" : "CD: FAIL\n");
    return ok != 1;
}
