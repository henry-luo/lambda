// grep_literal.cpp — required-literal extraction from RE2's parse tree (§5.2).
//
// This is the only file that includes RE2's internal re2/regexp.h (GRP7). It
// walks the tree RE2::Regexp() returns and computes, for each node, a small
// set of byte strings of which every match of the node must contain one. A
// node kind the walk does not understand yields "no set", which is always
// sound: the plan then falls back to RE2 searching the buffer (tier 2).

#include "grep_internal.hpp"
#include "../byte_builder.h"
#include "../log.h"

#include <re2/re2.h>
#include <re2/regexp.h>
#include <string.h>

namespace {

struct Lit {
    size_t off;      // into Ctx::bytes
    size_t len;
    bool fold;       // ASCII case-insensitive
};

// A set is a run of the context's literal pool, so a set of up to 64 literals
// costs a recursion frame a few words (RE2 allows nesting 1000 deep).
struct LitSet {
    bool none;       // no usable set
    int start;       // into Ctx::lits
    int count;
};

struct Ctx {
    ByteBuilder bytes;
    Lit* lits;
    int nlits, cap;
    bool oom;
    bool has_text_anchor;
    bool has_end_line;
};

// RE2 bounds nesting at 1000; stay below it without relying on that
const int LITERAL_MAX_DEPTH = 1000;
// a single-byte literal is worth scanning for only if the byte is rare
const uint8_t LITERAL_RARE_RANK = 200;

LitSet lit_none() {
    LitSet s = {true, 0, 0};
    return s;
}

// appends a literal to the pool; -1 on allocation failure
int lit_push(Ctx* ctx, Lit lit) {
    if (ctx->nlits == ctx->cap) {
        int cap = ctx->cap ? ctx->cap * 2 : 32;
        Lit* grown = (Lit*)mem_realloc(ctx->lits, (size_t)cap * sizeof(Lit), MEM_CAT_TEMP);
        if (!grown) {
            ctx->oom = true;
            return -1;
        }
        ctx->lits = grown;
        ctx->cap = cap;
    }
    ctx->lits[ctx->nlits] = lit;
    return ctx->nlits++;
}

uint8_t lit_rank(const Ctx* ctx, const Lit& lit) {
    StrNeedle needle;
    str_needle_init(&needle, (const char*)ctx->bytes.data + lit.off, lit.len, lit.fold);
    return needle.rare_rank;
}

// lexicographic quality: longer weakest literal (capped, since past a few bytes
// rarity matters more), then a rarer worst scan byte, then fewer literals
bool lit_better(const Ctx* ctx, const LitSet& a, const LitSet& b) {
    if (a.none) return false;
    if (b.none) return true;
    size_t a_min = SIZE_MAX, b_min = SIZE_MAX;
    uint8_t a_rank = 0, b_rank = 0;
    for (int i = 0; i < a.count; i++) {
        const Lit& lit = ctx->lits[a.start + i];
        if (lit.len < a_min) a_min = lit.len;
        uint8_t r = lit_rank(ctx, lit);
        if (r > a_rank) a_rank = r;
    }
    for (int i = 0; i < b.count; i++) {
        const Lit& lit = ctx->lits[b.start + i];
        if (lit.len < b_min) b_min = lit.len;
        uint8_t r = lit_rank(ctx, lit);
        if (r > b_rank) b_rank = r;
    }
    if (a_min > 4) a_min = 4;
    if (b_min > 4) b_min = 4;
    if (a_min != b_min) return a_min > b_min;
    if (a_rank != b_rank) return a_rank < b_rank;
    return a.count < b.count;
}

// One literal string from RE2 runes. A run is cut at a line terminator byte
// (it can never be inside a line match), and under case folding at a non-ASCII
// rune: ASCII-insensitive comparison is exact only for ASCII letters (RE2
// turns k and s, whose fold orbits leave ASCII, into classes), so a non-ASCII
// rune is left out rather than trusted (§5.4). The best run wins.
LitSet lit_from_runes(Ctx* ctx, const re2::Rune* runes, int nrunes, bool fold) {
    LitSet best = lit_none();
    size_t run_start = ctx->bytes.length;
    for (int i = 0; i <= nrunes; i++) {
        bool cut = (i == nrunes);
        if (!cut) {
            re2::Rune r = runes[i];
            if (r == '\n' || r == '\r' || r < 0 || (fold && r >= 0x80)) cut = true;
        }
        if (cut) {
            size_t run_len = ctx->bytes.length - run_start;
            if (run_len > 0) {
                Lit lit = {run_start, run_len, fold};
                int idx = lit_push(ctx, lit);
                if (idx < 0) return lit_none();
                LitSet one = {false, idx, 1};
                if (lit_better(ctx, one, best)) best = one;
            }
            run_start = ctx->bytes.length;
            continue;
        }
        char buf[4];
        size_t n = str_utf8_encode((uint32_t)runes[i], buf, sizeof(buf));
        if (n == 0 || !byte_builder_append(&ctx->bytes, buf, n)) {
            ctx->oom = (n != 0);
            return lit_none();
        }
    }
    return best;
}

LitSet lit_walk(Ctx* ctx, re2::Regexp* re, int depth) {
    if (!re || depth > LITERAL_MAX_DEPTH || ctx->oom) return lit_none();
    bool fold = (re->parse_flags() & re2::Regexp::FoldCase) != 0;
    switch (re->op()) {
    case re2::kRegexpLiteral: {
        re2::Rune r = re->rune();
        return lit_from_runes(ctx, &r, 1, fold);
    }
    case re2::kRegexpLiteralString:
        return lit_from_runes(ctx, re->runes(), re->nrunes(), fold);
    case re2::kRegexpConcat: {
        // every match contains every child's match: the best child's set serves
        LitSet best = lit_none();
        for (int i = 0; i < re->nsub(); i++) {
            LitSet s = lit_walk(ctx, re->sub()[i], depth + 1);
            if (lit_better(ctx, s, best)) best = s;
        }
        return best;
    }
    case re2::kRegexpAlternate: {
        // a match comes from one branch: the union serves, if every branch has one
        int nsub = re->nsub();
        if (nsub <= 0 || nsub > GREP_MAX_LITERALS) return lit_none();
        LitSet* kids = (LitSet*)mem_alloc((size_t)nsub * sizeof(LitSet), MEM_CAT_TEMP);
        if (!kids) {
            ctx->oom = true;
            return lit_none();
        }
        int total = 0;
        bool usable = true;
        for (int i = 0; i < nsub && usable; i++) {
            kids[i] = lit_walk(ctx, re->sub()[i], depth + 1);
            usable = !kids[i].none && total + kids[i].count <= GREP_MAX_LITERALS;
            if (usable) total += kids[i].count;
        }
        LitSet all = lit_none();
        if (usable && total > 0) {
            // the children's runs lie apart in the pool; copy them into one
            all.none = false;
            all.start = ctx->nlits;
            all.count = total;
            for (int i = 0; i < nsub && !ctx->oom; i++) {
                for (int j = 0; j < kids[i].count; j++) {
                    Lit lit = ctx->lits[kids[i].start + j];
                    if (lit_push(ctx, lit) < 0) break;
                }
            }
            if (ctx->oom) all = lit_none();
        }
        mem_free(kids);
        return all;
    }
    case re2::kRegexpPlus:
    case re2::kRegexpCapture:
        return re->nsub() > 0 ? lit_walk(ctx, re->sub()[0], depth + 1) : lit_none();
    case re2::kRegexpRepeat:
        // x{0,n} may match empty, so only a repeat of at least one carries x's set
        return (re->min() >= 1 && re->nsub() > 0) ? lit_walk(ctx, re->sub()[0], depth + 1) : lit_none();
    default:
        // star, quest, classes, any-char, anchors, empty and no-match: no set
        return lit_none();
    }
}

void scan_anchors(Ctx* ctx, re2::Regexp* re, int depth) {
    if (!re || depth > LITERAL_MAX_DEPTH) return;
    switch (re->op()) {
    case re2::kRegexpBeginText:
    case re2::kRegexpEndText:
        ctx->has_text_anchor = true;
        return;
    case re2::kRegexpEndLine:
        ctx->has_end_line = true;
        return;
    case re2::kRegexpConcat:
    case re2::kRegexpAlternate:
    case re2::kRegexpStar:
    case re2::kRegexpPlus:
    case re2::kRegexpQuest:
    case re2::kRegexpRepeat:
    case re2::kRegexpCapture:
        for (int i = 0; i < re->nsub(); i++) scan_anchors(ctx, re->sub()[i], depth + 1);
        return;
    default:
        return;
    }
}

// appends `len` bytes from ctx->bytes at `off` and then `len2` at `off2` as a
// new literal (the pieces may lie anywhere earlier in the buffer)
int lit_concat(Ctx* ctx, const Lit& a, const Lit& b) {
    size_t off = ctx->bytes.length;
    // reserve first: appending may move the buffer the pieces live in
    if (!byte_builder_reserve(&ctx->bytes, a.len + b.len)) {
        ctx->oom = true;
        return -1;
    }
    byte_builder_append(&ctx->bytes, ctx->bytes.data + a.off, a.len);
    byte_builder_append(&ctx->bytes, ctx->bytes.data + b.off, b.len);
    Lit lit = {off, a.len + b.len, false};
    return lit_push(ctx, lit);
}

// Tier 0 for a set: the finite language a node denotes, in RE2's preference
// order, when it is a few case-sensitive literals (one literal, an alternation
// of them, or a concatenation of such pieces as RE2 factors `ab|ac` into
// `a(?:b|c)`). Concatenation takes choices in order, so the list reproduces
// leftmost-first: at a position, the first string of the list that occurs
// there is the one RE2 would match. Anything else, including case folding,
// is not a pure set.
bool lit_expand(Ctx* ctx, re2::Regexp* re, int depth, LitSet* out) {
    if (!re || depth > LITERAL_MAX_DEPTH || ctx->oom) return false;
    if (re->parse_flags() & re2::Regexp::FoldCase) {
        if (re->op() == re2::kRegexpLiteral || re->op() == re2::kRegexpLiteralString) return false;
    }
    switch (re->op()) {
    case re2::kRegexpLiteral:
    case re2::kRegexpLiteralString: {
        re2::Rune single = re->op() == re2::kRegexpLiteral ? re->rune() : 0;
        const re2::Rune* runes = re->op() == re2::kRegexpLiteral ? &single : re->runes();
        int nrunes = re->op() == re2::kRegexpLiteral ? 1 : re->nrunes();
        size_t off = ctx->bytes.length;
        for (int i = 0; i < nrunes; i++) {
            char buf[4];
            if (runes[i] == '\n' || runes[i] == '\r' || runes[i] < 0) return false;
            size_t n = str_utf8_encode((uint32_t)runes[i], buf, sizeof(buf));
            if (n == 0) return false;
            if (!byte_builder_append(&ctx->bytes, buf, n)) {
                ctx->oom = true;
                return false;
            }
        }
        Lit lit = {off, ctx->bytes.length - off, false};
        int idx = lit_push(ctx, lit);
        if (idx < 0) return false;
        *out = (LitSet){false, idx, 1};
        return true;
    }
    case re2::kRegexpEmptyMatch: {
        Lit lit = {ctx->bytes.length, 0, false};
        int idx = lit_push(ctx, lit);
        if (idx < 0) return false;
        *out = (LitSet){false, idx, 1};
        return true;
    }
    case re2::kRegexpCapture:
        return re->nsub() > 0 && lit_expand(ctx, re->sub()[0], depth + 1, out);
    case re2::kRegexpAlternate: {
        int nsub = re->nsub();
        if (nsub <= 0 || nsub > GREP_MAX_LITERALS) return false;
        LitSet* kids = (LitSet*)mem_alloc((size_t)nsub * sizeof(LitSet), MEM_CAT_TEMP);
        if (!kids) {
            ctx->oom = true;
            return false;
        }
        int total = 0;
        bool ok = true;
        for (int i = 0; i < nsub && ok; i++) {
            ok = lit_expand(ctx, re->sub()[i], depth + 1, &kids[i]) && total + kids[i].count <= GREP_MAX_LITERALS;
            if (ok) total += kids[i].count;
        }
        if (ok) {
            out->none = false;
            out->start = ctx->nlits;
            out->count = total;
            for (int i = 0; i < nsub && ok; i++) {
                for (int j = 0; j < kids[i].count && ok; j++) ok = lit_push(ctx, ctx->lits[kids[i].start + j]) >= 0;
            }
        }
        mem_free(kids);
        return ok;
    }
    case re2::kRegexpConcat: {
        // the cross product, choices in order: earlier pieces vary slowest
        Lit empty = {ctx->bytes.length, 0, false};
        int seed = lit_push(ctx, empty);
        if (seed < 0) return false;
        LitSet acc = {false, seed, 1};
        for (int i = 0; i < re->nsub(); i++) {
            LitSet piece;
            if (!lit_expand(ctx, re->sub()[i], depth + 1, &piece)) return false;
            if ((long)acc.count * piece.count > GREP_MAX_LITERALS) return false;
            int start = ctx->nlits;
            for (int a = 0; a < acc.count; a++) {
                for (int b = 0; b < piece.count; b++) {
                    Lit la = ctx->lits[acc.start + a], lb = ctx->lits[piece.start + b];
                    if (lit_concat(ctx, la, lb) < 0) return false;
                }
            }
            acc = (LitSet){false, start, acc.count * piece.count};
        }
        *out = acc;
        return true;
    }
    default:
        return false;
    }
}

void ctx_release(Ctx* ctx) {
    byte_builder_destroy(&ctx->bytes);
    if (ctx->lits) mem_free(ctx->lits);
    ctx->lits = NULL;
}

}  // namespace

bool grep_literal_plan(const re2::RE2* re, GrepLiteralPlan* plan) {
    memset(plan, 0, sizeof(*plan));
    plan->tier = GREP_TIER_REGEX;
    re2::Regexp* root = re ? re->Regexp() : NULL;
    if (!root) return true;

    Ctx ctx;
    memset(&ctx, 0, sizeof(ctx));
    if (!byte_builder_init(&ctx.bytes, 64, MEM_CAT_TEMP, false)) return false;
    scan_anchors(&ctx, root, 0);
    plan->has_text_anchor = ctx.has_text_anchor;
    plan->has_end_line = ctx.has_end_line;

    LitSet chosen = lit_none();
    GrepTier tier = GREP_TIER_REGEX;
    if (lit_expand(&ctx, root, 0, &chosen) && chosen.count >= 1) {
        // a pure set of non-empty literals needs no RE2 at all
        tier = GREP_TIER_LITERAL;
        for (int i = 0; i < chosen.count; i++) {
            if (ctx.lits[chosen.start + i].len == 0) tier = GREP_TIER_REGEX;
        }
    }
    if (ctx.oom) {
        ctx_release(&ctx);
        return false;
    }
    if (tier != GREP_TIER_LITERAL) {
        chosen = lit_walk(&ctx, root, 0);
        if (!chosen.none) {
            size_t min_len = SIZE_MAX;
            uint8_t worst = 0;
            for (int i = 0; i < chosen.count; i++) {
                const Lit& lit = ctx.lits[chosen.start + i];
                if (lit.len < min_len) min_len = lit.len;
                uint8_t r = lit_rank(&ctx, lit);
                if (r > worst) worst = r;
            }
            if (min_len >= 2 || worst < LITERAL_RARE_RANK) tier = GREP_TIER_REQUIRED;
        }
    }
    if (ctx.oom) {
        ctx_release(&ctx);
        return false;
    }
    if (tier != GREP_TIER_REGEX) {
        size_t total = 0;
        for (int i = 0; i < chosen.count; i++) total += ctx.lits[chosen.start + i].len;
        plan->storage = (char*)mem_alloc(total ? total : 1, MEM_CAT_TEMP);
        if (!plan->storage) {
            ctx_release(&ctx);
            return false;
        }
        const char* bytes[GREP_MAX_LITERALS];
        size_t lens[GREP_MAX_LITERALS];
        bool folds[GREP_MAX_LITERALS];
        size_t at = 0;
        for (int i = 0; i < chosen.count; i++) {
            const Lit& lit = ctx.lits[chosen.start + i];
            memcpy(plan->storage + at, ctx.bytes.data + lit.off, lit.len);
            str_needle_init(&plan->needles[i], plan->storage + at, lit.len, lit.fold);
            bytes[i] = plan->storage + at;
            lens[i] = lit.len;
            folds[i] = lit.fold;
            at += lit.len;
        }
        plan->count = chosen.count;
        // two or more literals are searched together (GRP29)
        if (chosen.count >= 2 && !str_teddy_init(&plan->teddy, bytes, lens, folds, chosen.count)) {
            tier = GREP_TIER_REGEX;
            plan->count = 0;
        }
        plan->tier = tier;
    }
    log_debug("grep plan: tier=%d literals=%d text_anchor=%d end_line=%d",
              (int)plan->tier, plan->count, (int)plan->has_text_anchor, (int)plan->has_end_line);
    ctx_release(&ctx);
    return true;
}

void grep_literal_plan_release(GrepLiteralPlan* plan) {
    if (!plan) return;
    if (plan->storage) mem_free(plan->storage);
    plan->storage = NULL;
    plan->count = 0;
}
