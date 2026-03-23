#ifndef CONTEXT_H
#define CONTEXT_H
#include <stdint.h>

typedef uint64_t u64;

/**
 * aarch64 registers:
 * x0-x7        general purpose, used for arguments, return values
 * x8           syscall number
 * x9-x15       general purpose, used for temporary data storage
 * x16-x17      temporary registers, scratch
 * x18          platform register, platform-specific use cases
 * x19-x28      callee-saved registers
 * x29 (fp)     frame pointer, points to start of stack frame
 * x30 (lr)     link register, hold return address
 * sp           stack pointer, points to top of stack
 * pc           program counter, holds next instruction address
 */

typedef struct {
        u64     x19, x20, x21, x22;     // callee-saved regs
        u64     x23, x24, x25, x26;     // callee-saved regs
        u64     x27, x28;               // callee-saved regs
        u64     fp;                     // frame pointer
        u64     lr;                     // link register
        u64     sp;                     // stack pointer
        double  d8, d9, d10, d11;       // floating point regs
        double  d12, d13, d14, d15;     // floating point regs
} context_t;

int getcontext(context_t *ctx);
void setcontext(context_t *ctx);

#endif
