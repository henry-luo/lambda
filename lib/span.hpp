#pragma once
// Bounded, non-owning views — the "how big is it" safety axis (Rust's &[T] slice).
// Span<T>  : a (ptr, len) pair with bounds-checked access.
// ByteCursor: a (cur, end) pair for recursive-descent parsers — advancing past the
//             buffer is structurally impossible, so "read past end-of-input" cannot happen.
// Both are pointer-pair sized, trivially copyable, and decay to raw pointers at the C/MIR edge.
// See vibe/Memory_Safety_Template3.md §3.2.

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>   // abort

#include "log.h"      // log_error (distinct "SPAN-OOB:" prefix per CLAUDE rule 9)

namespace lam {

// Out-of-bounds is a fail-closed abort in every build — corruption must fail early, never
// silently. Hot paths that have already proven the index in range use raw()/unchecked().
#define LAM_SPAN_OOB(idx, len) \
    do { log_error("SPAN-OOB: index %zu out of bounds (len %zu)", (size_t)(idx), (size_t)(len)); abort(); } while (0)

template<class T>
struct Span {
    T*     data_ = nullptr;
    size_t len_  = 0;

    constexpr Span() = default;
    constexpr Span(T* data, size_t len) : data_(data), len_(len) {}

    constexpr size_t size()  const { return len_; }
    constexpr bool   empty() const { return len_ == 0; }
    T*    raw()   const { return data_; }            // explicit decay at the ABI edge
    T*    begin() const { return data_; }            // range-for
    T*    end()   const { return data_ + len_; }

    // checked access — aborts on OOB.
    T& operator[](size_t i) const {
        if (i >= len_) LAM_SPAN_OOB(i, len_);
        return data_[i];
    }
    T& at(size_t i) const { return (*this)[i]; }

    // non-aborting probe: returns false on OOB, leaving *out untouched.
    bool get(size_t i, T* out) const {
        if (i >= len_) return false;
        *out = data_[i];
        return true;
    }

    // explicitly unchecked — caller asserts i < len_. Greppable for audit.
    T& unchecked(size_t i) const { return data_[i]; }

    // clamped sub-view — never produces an out-of-range Span.
    Span subspan(size_t off, size_t n) const {
        if (off > len_) off = len_;
        if (n > len_ - off) n = len_ - off;
        return Span(data_ + off, n);
    }
    Span first(size_t n) const { return subspan(0, n); }
};

// A byte cursor over [p_, end_). All reads are bounds-aware; advancing returns false
// rather than walking off the end.
struct ByteCursor {
    const uint8_t* p_   = nullptr;
    const uint8_t* end_ = nullptr;

    constexpr ByteCursor() = default;
    constexpr ByteCursor(const uint8_t* p, const uint8_t* end) : p_(p), end_(end) {}
    static ByteCursor of(const void* data, size_t len) {
        const uint8_t* b = (const uint8_t*)data;
        return ByteCursor(b, b + len);
    }

    size_t remaining() const { return (size_t)(end_ - p_); }
    bool   eof()  const { return p_ >= end_; }
    bool   has(size_t n) const { return remaining() >= n; }
    const uint8_t* cur() const { return p_; }

    // peek i bytes ahead; returns 0 past the end instead of reading out of bounds.
    uint8_t peek(size_t i = 0) const { return (p_ + i < end_) ? p_[i] : (uint8_t)0; }

    // advance n bytes only if they exist; false (no movement) otherwise.
    bool advance(size_t n) {
        if (!has(n)) return false;
        p_ += n;
        return true;
    }

    // consume one byte into *out; false at eof.
    bool take(uint8_t* out) {
        if (p_ >= end_) return false;
        *out = *p_++;
        return true;
    }
};

} // namespace lam
