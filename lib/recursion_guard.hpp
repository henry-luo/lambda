#pragma once
// Stack-scoped recursion-depth guard — the "recursion / DoS" safety axis.
// One guard at each recursive-descent / AST-walk entry caps depth, turning an unbounded
// recursion (stack overflow / UB) into a normal, reported error. RAII keeps the depth
// counter balanced on every exit path, including the over-limit one.
// See vibe/Memory_Safety_Template3.md §3.5.
//
//   RecursionGuard g(&ctx->depth, MAX_PARSE_DEPTH);
//   if (!g) return parse_error(ctx, ERR_RECURSION_LIMIT);   // too deep — bail
//   ... recurse ...

namespace lam {

struct RecursionGuard {
    int* depth_;
    bool ok_;

    RecursionGuard(int* depth, int limit)
        : depth_(depth), ok_(++(*depth) <= limit) {}

    ~RecursionGuard() { --(*depth_); }

    RecursionGuard(const RecursionGuard&) = delete;
    RecursionGuard& operator=(const RecursionGuard&) = delete;

    // true while within the limit; false once the limit is exceeded.
    explicit operator bool() const { return ok_; }
    int depth() const { return *depth_; }
};

} // namespace lam
