#include "asm_help.h"

.global __xnd_setcontext 
.align 2
__xnd_setcontext:
        // x0 = mcontext_t
        // restore callee-saved registers
        ldp x19, x20, [x0, MCONTEXT_OFFSET_X19_X20]
        ldp x21, x22, [x0, MCONTEXT_OFFSET_X21_X22]
        ldp x23, x24, [x0, MCONTEXT_OFFSET_X23_X24]
        ldp x25, x26, [x0, MCONTEXT_OFFSET_X25_X26]
        ldp x27, x28, [x0, MCONTEXT_OFFSET_X27_X28]
        
        // restore fp, lr, sp
        ldp x10, x11, [x0, MCONTEXT_OFFSET_FP_LR]
        ldr x12, [x0, MCONTEXT_OFFSET_SP]
        mov fp, x10
        mov lr, x11
        mov sp, x12
        
        // restore neon/fp registers
        ldr d0, [x0, MCONTEXT_OFFSET_D0]
        ldr d1, [x0, MCONTEXT_OFFSET_D1]
        ldr d2, [x0, MCONTEXT_OFFSET_D2]
        ldr d3, [x0, MCONTEXT_OFFSET_D3]
        ldr d4, [x0, MCONTEXT_OFFSET_D4]
        ldr d5, [x0, MCONTEXT_OFFSET_D5]
        ldr d6, [x0, MCONTEXT_OFFSET_D6]
        ldr d7, [x0, MCONTEXT_OFFSET_D7]
        ldr d8, [x0, MCONTEXT_OFFSET_D8]
        ldr d9, [x0, MCONTEXT_OFFSET_D9]
        ldr d10, [x0, MCONTEXT_OFFSET_D10]
        ldr d11, [x0, MCONTEXT_OFFSET_D11]
        ldr d12, [x0, MCONTEXT_OFFSET_D12]
        ldr d13, [x0, MCONTEXT_OFFSET_D13]
        ldr d14, [x0, MCONTEXT_OFFSET_D14]
        ldr d15, [x0, MCONTEXT_OFFSET_D15]
        
        // restore argument/scratch registers
        ldp x1, x2, [x0, MCONTEXT_OFFSET_X1_X2]
        ldp x3, x4, [x0, MCONTEXT_OFFSET_X3_X4]
        ldp x5, x6, [x0, MCONTEXT_OFFSET_X5_X6]
        ldp x7, x8, [x0, MCONTEXT_OFFSET_X7_X8]
        ldp x9, x10, [x0, MCONTEXT_OFFSET_X9_X10]
        ldp x11, x12, [x0, MCONTEXT_OFFSET_X11_X12]
        ldp x13, x14, [x0, MCONTEXT_OFFSET_X13_X14]
        ldp x15, x16, [x0, MCONTEXT_OFFSET_X15_X16]
        ldr x17, [x0, MCONTEXT_OFFSET_X17]
        ldr x0, [x0, MCONTEXT_OFFSET_X0]

        ret 
