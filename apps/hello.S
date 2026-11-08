.section .rodata
message:
    .ascii "BORDIFY USERMODE: hello from an isolated ring-3 ELF32 app\n"
message_end:

.section .text
.global _start
.type _start, @function
_start:
    movl $1, %eax           /* Bordify syscall write */
    movl $message, %ebx
    movl $message_end-message, %ecx
    int $0x80

    movl $2, %eax           /* Bordify syscall exit */
    xorl %ebx, %ebx
    int $0x80
1:
    jmp 1b
.size _start, . - _start
