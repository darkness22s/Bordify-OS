# Multiboot2 entry point for Bordify OS.

.set MB2_MAGIC, 0xE85250D6
.set MB2_ARCH, 0

.section .multiboot, "a"
.align 8
mb2_header_start:
    .long MB2_MAGIC
    .long MB2_ARCH
    .long mb2_header_end - mb2_header_start
    .long -(MB2_MAGIC + MB2_ARCH + (mb2_header_end - mb2_header_start))

    /* Request a 1024x768 32-bit linear framebuffer. */
    .short 5
    .short 0
    .long 20
    .long 1024
    .long 768
    .long 32
    .long 0             /* align the end tag to 8 bytes */

    .short 0
    .short 0
    .long 8
mb2_header_end:

.section .bss
.align 16
stack_bottom:
    .skip 65536
stack_top:

.section .text
.global _start
.type _start, @function
_start:
    cli
    mov $stack_top, %esp
    and $-16, %esp
    xor %ebp, %ebp
    mov %ebx, %edi
    sub $12, %esp
    push %edi
    call kernel_main
1:
    hlt
    jmp 1b

.size _start, . - _start

.section .note.GNU-stack,"",@progbits
