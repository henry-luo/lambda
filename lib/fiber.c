#include "fiber.h"

#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#if !defined(MAP_ANON) && defined(MAP_ANONYMOUS)
#define MAP_ANON MAP_ANONYMOUS
#endif
#endif

size_t fiber_page_size(void) {
#if defined(_WIN32)
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    return info.dwPageSize ? (size_t)info.dwPageSize : 4096u;
#else
    long page = sysconf(_SC_PAGESIZE);
    return page > 0 ? (size_t)page : 4096u;
#endif
}

bool fiber_stack_reserve(FiberStack* stack, size_t usable_bytes, size_t tail_bytes) {
    if (!stack) return false;
    memset(stack, 0, sizeof(*stack));
    size_t page = fiber_page_size();
    size_t usable = (usable_bytes + page - 1) & ~(page - 1);
    size_t tail = (tail_bytes + page - 1) & ~(page - 1);
    size_t reserved = page + usable + tail;
#if defined(_WIN32)
    // Windows grows a thread stack through its own guard-page protocol; a
    // fiber stack has no such owner, so commit it up front. Physical pages
    // are still only provided on first touch.
    void* memory = VirtualAlloc(NULL, reserved, MEM_RESERVE | MEM_COMMIT,
                                PAGE_READWRITE);
    if (!memory) return false;
    DWORD old_protect = 0;
    if (!VirtualProtect(memory, page, PAGE_NOACCESS, &old_protect)) {
        VirtualFree(memory, 0, MEM_RELEASE);
        return false;
    }
#else
    int flags = MAP_PRIVATE | MAP_ANON;
#if defined(MAP_NORESERVE)
    flags |= MAP_NORESERVE;
#endif
    void* memory = mmap(NULL, reserved, PROT_READ | PROT_WRITE, flags, -1, 0);
    if (memory == MAP_FAILED) return false;
    // The low page faults on overflow; the runtime's SIGSEGV handler turns a
    // fault there into a recoverable stack-overflow landing.
    if (mprotect(memory, page, PROT_NONE) != 0) {
        munmap(memory, reserved);
        return false;
    }
#endif
    stack->reservation = memory;
    stack->reserved = reserved;
    stack->low = (uintptr_t)memory + page;
    stack->high = stack->low + usable;
    stack->tail_bytes = tail;
    return true;
}

void fiber_stack_release(FiberStack* stack) {
    if (!stack || !stack->reservation) return;
#if defined(_WIN32)
    VirtualFree(stack->reservation, 0, MEM_RELEASE);
#else
    munmap(stack->reservation, stack->reserved);
#endif
    memset(stack, 0, sizeof(*stack));
}

void fiber_memory_discard(void* addr, size_t len) {
    if (!addr || !len) return;
#if defined(_WIN32)
    VirtualAlloc(addr, len, MEM_RESET, PAGE_READWRITE);
#elif defined(MADV_FREE_REUSABLE)
    // macOS only drops a page from the footprint for a reusable discard;
    // MADV_DONTNEED leaves anonymous pages accounted.
    madvise(addr, len, MADV_FREE_REUSABLE);
#else
    madvise(addr, len, MADV_DONTNEED);
#endif
}

void fiber_memory_reclaim(void* addr, size_t len) {
#if defined(MADV_FREE_REUSE)
    if (addr && len) madvise(addr, len, MADV_FREE_REUSE);
#else
    (void)addr;
    (void)len;
#endif
}

void fiber_stack_trim(FiberStack* stack, size_t keep_bytes) {
    if (!stack || !stack->reservation) return;
    size_t page = fiber_page_size();
    uintptr_t keep = (keep_bytes + page - 1) & ~(uintptr_t)(page - 1);
    if (stack->high - stack->low <= keep) return;
    uintptr_t end = stack->high - keep;
    fiber_memory_discard((void*)stack->low, end - stack->low);
    fiber_memory_reclaim((void*)stack->low, end - stack->low);
}

// ---------------------------------------------------------------------------
// Context switch. Each variant pushes the ABI's callee-saved registers on the
// current stack, publishes the stack pointer, adopts the target stack and pops
// the same layout. A primed stack holds that layout with a return address of
// fiber_trampoline, which moves the saved (entry, arg) pair into argument
// registers and calls the entry.
// ---------------------------------------------------------------------------

#if defined(__APPLE__)
#define FIBER_SYM(name) "_" #name
#define FIBER_FUNC_BEGIN(name) ".globl " FIBER_SYM(name) "\n" ".p2align 4\n" FIBER_SYM(name) ":\n"
#elif defined(_WIN32)
#define FIBER_SYM(name) #name
#define FIBER_FUNC_BEGIN(name) ".globl " FIBER_SYM(name) "\n" ".p2align 4\n" FIBER_SYM(name) ":\n"
#else
#define FIBER_SYM(name) #name
#define FIBER_FUNC_BEGIN(name) ".globl " FIBER_SYM(name) "\n" ".type " FIBER_SYM(name) ", %function\n" ".p2align 4\n" FIBER_SYM(name) ":\n"
#endif

// Global, not a local label: link-time optimization can inline the priming
// code into another object, which then needs to resolve this symbol.
void fiber_trampoline(void);

#if defined(__aarch64__) || defined(__arm64__)

// AAPCS64: x19-x28, x29 (fp), x30 (lr) and the low halves d8-d15. x18 is
// reserved by Apple and never touched.
#define FIBER_FRAME_WORDS 20

__asm__(
    ".text\n"
    FIBER_FUNC_BEGIN(fiber_switch)
    "sub sp, sp, #160\n"
    "stp x19, x20, [sp, #0]\n"
    "stp x21, x22, [sp, #16]\n"
    "stp x23, x24, [sp, #32]\n"
    "stp x25, x26, [sp, #48]\n"
    "stp x27, x28, [sp, #64]\n"
    "stp x29, x30, [sp, #80]\n"
    "stp d8, d9, [sp, #96]\n"
    "stp d10, d11, [sp, #112]\n"
    "stp d12, d13, [sp, #128]\n"
    "stp d14, d15, [sp, #144]\n"
    "mov x9, sp\n"
    "str x9, [x0]\n"
    "mov sp, x1\n"
    "ldp x19, x20, [sp, #0]\n"
    "ldp x21, x22, [sp, #16]\n"
    "ldp x23, x24, [sp, #32]\n"
    "ldp x25, x26, [sp, #48]\n"
    "ldp x27, x28, [sp, #64]\n"
    "ldp x29, x30, [sp, #80]\n"
    "ldp d8, d9, [sp, #96]\n"
    "ldp d10, d11, [sp, #112]\n"
    "ldp d12, d13, [sp, #128]\n"
    "ldp d14, d15, [sp, #144]\n"
    "add sp, sp, #160\n"
    "ret\n"
    FIBER_FUNC_BEGIN(fiber_trampoline)
    "mov x0, x20\n"
    "blr x19\n"
    "brk #0\n"
    // x29 is callee-saved, so it carries the caller's stack pointer across fn.
    FIBER_FUNC_BEGIN(fiber_call_on)
    "stp x29, x30, [sp, #-16]!\n"
    "mov x29, sp\n"
    "and x9, x0, #0xfffffffffffffff0\n"
    "mov sp, x9\n"
    "mov x0, x2\n"
    "blr x1\n"
    "mov sp, x29\n"
    "ldp x29, x30, [sp], #16\n"
    "ret\n"
);

void* fiber_stack_prime(FiberStack* stack, FiberEntry entry, void* arg) {
    uintptr_t top = stack->high & ~(uintptr_t)15;
    uint64_t* frame = (uint64_t*)(top - FIBER_FRAME_WORDS * sizeof(uint64_t));
    memset(frame, 0, FIBER_FRAME_WORDS * sizeof(uint64_t));
    frame[0] = (uint64_t)(uintptr_t)entry;             // x19
    frame[1] = (uint64_t)(uintptr_t)arg;               // x20
    frame[10] = 0;                                     // x29: ends backtraces
    frame[11] = (uint64_t)(uintptr_t)fiber_trampoline; // x30
    return frame;
}

#elif (defined(__x86_64__) || defined(_M_X64)) && defined(_WIN32)

// Microsoft x64 (GNU assembler): rbx, rbp, rdi, rsi, r12-r15, xmm6-xmm15,
// MXCSR and the x87 control word, plus the TIB stack bounds that SEH and the
// stack probes read (StackBase gs:0x08, StackLimit gs:0x10,
// DeallocationStack gs:0x1478). Unverified: no Windows CI in this change.
#define FIBER_FRAME_WORDS 33

__asm__(
    ".text\n"
    FIBER_FUNC_BEGIN(fiber_switch)
    "pushq %rbp\n" "pushq %rbx\n" "pushq %rdi\n" "pushq %rsi\n"
    "pushq %r12\n" "pushq %r13\n" "pushq %r14\n" "pushq %r15\n"
    "movq %gs:0x1478, %rax\n" "pushq %rax\n"
    "movq %gs:0x10, %rax\n" "pushq %rax\n"
    "movq %gs:0x08, %rax\n" "pushq %rax\n"
    "subq $168, %rsp\n"
    "movups %xmm6, 0(%rsp)\n" "movups %xmm7, 16(%rsp)\n"
    "movups %xmm8, 32(%rsp)\n" "movups %xmm9, 48(%rsp)\n"
    "movups %xmm10, 64(%rsp)\n" "movups %xmm11, 80(%rsp)\n"
    "movups %xmm12, 96(%rsp)\n" "movups %xmm13, 112(%rsp)\n"
    "movups %xmm14, 128(%rsp)\n" "movups %xmm15, 144(%rsp)\n"
    "stmxcsr 160(%rsp)\n" "fnstcw 164(%rsp)\n"
    "movq %rsp, (%rcx)\n"
    "movq %rdx, %rsp\n"
    "movups 0(%rsp), %xmm6\n" "movups 16(%rsp), %xmm7\n"
    "movups 32(%rsp), %xmm8\n" "movups 48(%rsp), %xmm9\n"
    "movups 64(%rsp), %xmm10\n" "movups 80(%rsp), %xmm11\n"
    "movups 96(%rsp), %xmm12\n" "movups 112(%rsp), %xmm13\n"
    "movups 128(%rsp), %xmm14\n" "movups 144(%rsp), %xmm15\n"
    "ldmxcsr 160(%rsp)\n" "fldcw 164(%rsp)\n"
    "addq $168, %rsp\n"
    "popq %rax\n" "movq %rax, %gs:0x08\n"
    "popq %rax\n" "movq %rax, %gs:0x10\n"
    "popq %rax\n" "movq %rax, %gs:0x1478\n"
    "popq %r15\n" "popq %r14\n" "popq %r13\n" "popq %r12\n"
    "popq %rsi\n" "popq %rdi\n" "popq %rbx\n" "popq %rbp\n"
    "ret\n"
    FIBER_FUNC_BEGIN(fiber_trampoline)
    "movq %r13, %rcx\n"
    "subq $32, %rsp\n"
    "callq *%r12\n"
    "ud2\n"
);

void* fiber_call_on(void* stack_top, FiberCall fn, void* arg) {
    (void)stack_top;
    return fn(arg);
}

void* fiber_stack_prime(FiberStack* stack, FiberEntry entry, void* arg) {
    // `ret` leaves rsp at `top`; the trampoline's call then needs it 16-aligned.
    uintptr_t top = (stack->high & ~(uintptr_t)15) - 16;
    uint64_t* frame = (uint64_t*)(top - FIBER_FRAME_WORDS * sizeof(uint64_t));
    memset(frame, 0, FIBER_FRAME_WORDS * sizeof(uint64_t));
    frame[20] = 0x1F80u | ((uint64_t)0x037Fu << 32);  // MXCSR, x87 CW
    frame[21] = (uint64_t)stack->high;                // StackBase
    frame[22] = (uint64_t)stack->low;                 // StackLimit
    frame[23] = (uint64_t)(uintptr_t)stack->reservation; // DeallocationStack
    frame[26] = (uint64_t)(uintptr_t)arg;             // r13
    frame[27] = (uint64_t)(uintptr_t)entry;           // r12
    frame[32] = (uint64_t)(uintptr_t)fiber_trampoline;
    return frame;
}

#elif defined(__x86_64__)

// System V x86-64: rbx, rbp, r12-r15, MXCSR and the x87 control word.
#define FIBER_FRAME_WORDS 8

__asm__(
    ".text\n"
    FIBER_FUNC_BEGIN(fiber_switch)
    "pushq %rbp\n" "pushq %rbx\n"
    "pushq %r12\n" "pushq %r13\n" "pushq %r14\n" "pushq %r15\n"
    "subq $8, %rsp\n"
    "stmxcsr (%rsp)\n" "fnstcw 4(%rsp)\n"
    "movq %rsp, (%rdi)\n"
    "movq %rsi, %rsp\n"
    "ldmxcsr (%rsp)\n" "fldcw 4(%rsp)\n"
    "addq $8, %rsp\n"
    "popq %r15\n" "popq %r14\n" "popq %r13\n" "popq %r12\n"
    "popq %rbx\n" "popq %rbp\n"
    "ret\n"
    FIBER_FUNC_BEGIN(fiber_trampoline)
    "movq %r13, %rdi\n"
    "callq *%r12\n"
    "ud2\n"
    // rbp is callee-saved, so it carries the caller's stack pointer across fn;
    // a 16-aligned rsp at the call leaves fn at the ABI's 8-mod-16 entry.
    FIBER_FUNC_BEGIN(fiber_call_on)
    "pushq %rbp\n"
    "movq %rsp, %rbp\n"
    "andq $-16, %rdi\n"
    "movq %rdi, %rsp\n"
    "movq %rdx, %rdi\n"
    "callq *%rsi\n"
    "movq %rbp, %rsp\n"
    "popq %rbp\n"
    "ret\n"
);

void* fiber_stack_prime(FiberStack* stack, FiberEntry entry, void* arg) {
    // After `ret` pops the trampoline address the stack must be 16-aligned so
    // the trampoline's call leaves the entry at the ABI's 8-mod-16 position.
    uintptr_t aligned = (stack->high & ~(uintptr_t)15) - 16;
    uint64_t* frame = (uint64_t*)(aligned - FIBER_FRAME_WORDS * sizeof(uint64_t));
    memset(frame, 0, FIBER_FRAME_WORDS * sizeof(uint64_t));
    frame[0] = 0x1F80u | ((uint64_t)0x037Fu << 32);   // MXCSR, x87 CW
    frame[3] = (uint64_t)(uintptr_t)arg;              // r13
    frame[4] = (uint64_t)(uintptr_t)entry;            // r12
    frame[6] = 0;                                     // rbp: ends backtraces
    frame[7] = (uint64_t)(uintptr_t)fiber_trampoline;
    return frame;
}

#else
#error "fiber: no context switch for this architecture"
#endif
