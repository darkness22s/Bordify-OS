.section .text
.global _start
.type _start, @function
_start:
    /*
     * Deliberately read kernel-only address 0 to prove ring-3 cannot
     * access supervisor mappings; should generate a page fault (#PF).
     */
    movl 0, %eax
    movl $2, %eax
    xorl %ebx, %ebx
    int $0x80
1:
    jmp 1b
.size _start, . - _start
