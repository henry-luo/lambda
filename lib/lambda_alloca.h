// lib/lambda_alloca.h — bounded `alloca` macro.
//
// Lambda's MIR codegen and a handful of layout/runtime paths use `alloca` for
// scratch buffers sized by an in-bounds count (`argc`, `nparams`, `nops`,
// `stop_count`, ...). The size is bounded at the caller level but not by a
// compile-time constant, so the project's `alloca-static-size` lint rule
// flags every site as "potentially unbounded."
//
// LAMBDA_ALLOCA(n, T) wraps the common shape `(T*)alloca(n * sizeof(T))`
// with a per-call-site upper-bound assert. In debug builds an oversized call
// aborts at the bad site (testable); in release the macro decays to a plain
// alloca and gives up the diagnostic. Stack overflow on an *un*tested,
// adversarial input is still UB — the macro doesn't change that, it just
// makes the bound observable to tests.
//
// Important caveats:
//   • `n` must be side-effect-free: the macro evaluates it twice.
//   • `T` is a type, not an expression.
//   • Override the per-call ceiling by `#define LAMBDA_ALLOCA_MAX_BYTES …`
//     BEFORE including this header.

#ifndef LAMBDA_ALLOCA_H
#define LAMBDA_ALLOCA_H

#include <assert.h>
#include <stddef.h>
#ifdef _WIN32
#include <malloc.h>
#else
#include <alloca.h>
#endif

#ifndef LAMBDA_ALLOCA_MAX_BYTES
// 256 KiB — large enough for MIR codegen of complex expressions (a few
// thousand `MIR_op_t` entries at ~64 B each, observed in the math/latex
// transpile path), small enough that one frame can't single-handedly
// exhaust the typical 8 MiB stack. The intent is to catch *runaway*
// alloca() (millions of bytes from a corrupted count), not to enforce a
// tight per-page budget.
#define LAMBDA_ALLOCA_MAX_BYTES (256 * 1024)
#endif

#define LAMBDA_ALLOCA(n, T) \
    ((T*)(assert((size_t)(n) * sizeof(T) <= LAMBDA_ALLOCA_MAX_BYTES), \
          alloca((size_t)(n) * sizeof(T))))

#endif // LAMBDA_ALLOCA_H
