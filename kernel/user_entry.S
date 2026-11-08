.section .text
.global process_enter
.type process_enter, @function
process_enter:
    /* Preserve the caller's kernel stack/callee-saved registers. */
    pushl %ebx
    pushl %esi
    pushl %edi
    pushl %ebp
    movl %esp, saved_kernel_esp
    movl 20(%esp), %edx       /* user EIP */
    movl 24(%esp), %ecx       /* user ESP */

    /* iret performs the CPL0 -> CPL3 stack and code-segment transition. */
    pushl $0x23
    pushl %ecx
    pushfl
    popl %eax
    orl $0x200, %eax
    pushl %eax
    pushl $0x1b
    pushl %edx
    movw $0x23, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    iret
.size process_enter, . - process_enter

.global process_leave
.type process_leave, @function
process_leave:
    /* Called on the TSS ring-0 stack by a syscall or isolated user fault. */
    cli
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movl saved_kernel_esp, %esp
    popl %ebp
    popl %edi
    popl %esi
    popl %ebx
    sti
    ret
.size process_leave, . - process_leave

.section .bss
.align 4
saved_kernel_esp:
    .long 0
.section .note.GNU-stack,"",@progbits
