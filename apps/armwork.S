.text
.globl _main
.p2align 2
_main:
    // Standard ARM64 function prologue; stack must be guest-memory-only.
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    mov x9, #10
    mov x10, #0
sum_loop:
    add x10, x10, x9
    subs x9, x9, #1
    b.ne sum_loop
    cmp x10, #55
    b.ne failure
    sub sp, sp, #16
    str x10, [sp]
    ldr x11, [sp]
    add sp, sp, #16
    cmp x11, #55
    b.ne failure

    bl compute_one
    cmp x0, #1
    b.ne failure
    mov x0, #1
    adr x1, message
    mov x2, #63
    mov x16, #4        // Darwin BSD write(stdout,...)
    svc #0x80
    mov x0, #0
    mov x16, #1        // Darwin BSD exit(0)
    svc #0x80

compute_one:
    mov x0, #1
    ret

failure:
    mov x0, #99
    mov x16, #1
    svc #0x80

message:
    .ascii "BORDIFY ARM64 EXTENDED: verified arithmetic branches and stack\n"
