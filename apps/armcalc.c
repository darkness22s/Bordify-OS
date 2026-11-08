/* This is actual freestanding C code compiled with clang for
 * arm64-apple-macos11 (Mach-O ARM64), not hand-encoded machine code.
 *
 * A volatile accumulator forces the compiler to generate genuine loop,
 * stack-memory, arithmetic, CMP/B.cond and CSEL instructions.
 * It uses a tiny opt-in Darwin-style BSD exit syscall as the only OS call.
 */
#include <stdint.h>

__attribute__((noreturn)) static void exit_guest(unsigned status) {
    __asm__ volatile (
        "mov x0, %0\n"
        "mov x16, #1\n"
        "svc #0x80\n"
        :
        : "r"((uint64_t)status)
        : "x0", "x16", "memory");
    __builtin_unreachable();
}

int main(void) {
    volatile uint64_t sum=0;
    for (unsigned i=1; i<=10; ++i) {
        sum += i;
    }
    exit_guest(sum == 55 ? 0u : 99u);
}
