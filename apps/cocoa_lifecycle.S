/* Apple-targeted Clang ARM64 assembly with real unresolved ObjC symbols.
 *
 * linkcocoaobj.py validates LC_SYMTAB and ARM64_RELOC_BRANCH26 entries,
 * then statically binds _objc_getClass, _sel_registerName, _objc_msgSend
 * to three opt-in Bordify import trampolines. The guest uses standard
 * objc_msgSend x0 receiver / x1 selector / x2 first argument ABI.
 * This is a small Foundation/AppKit-like model, NOT macOS Cocoa itself.
 */
.text
.globl _main
.p2align 2
_main:
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    adr x0, name_nsstring
    bl _objc_getClass
    mov x19, x0
    adr x0, sel_create
    bl _sel_registerName
    mov x1, x0
    mov x0, x19
    adr x2, message
    bl _objc_msgSend
    mov x20, x0

    adr x0, sel_length
    bl _sel_registerName
    mov x1, x0
    mov x0, x20
    bl _objc_msgSend
    cmp x0, #66
    b.ne failure

    /* Create a modeled NSWindow, set NSString title, make visible. */
    adr x0, name_nswindow
    bl _objc_getClass
    mov x21, x0
    adr x0, sel_new
    bl _sel_registerName
    mov x1, x0
    mov x0, x21
    bl _objc_msgSend
    mov x21, x0
    adr x0, sel_settitle
    bl _sel_registerName
    mov x1, x0
    mov x0, x21
    mov x2, x20
    bl _objc_msgSend
    adr x0, sel_present
    bl _sel_registerName
    mov x1, x0
    mov x0, x21
    mov x2, xzr
    bl _objc_msgSend

    /* NSApplication lifecycle: initially stopped -> run -> terminate. */
    adr x0, name_nsapp
    bl _objc_getClass
    mov x22, x0
    adr x0, sel_shared
    bl _sel_registerName
    mov x1, x0
    mov x0, x22
    bl _objc_msgSend
    mov x22, x0
    adr x0, sel_running
    bl _sel_registerName
    mov x23, x0
    mov x1, x23
    mov x0, x22
    bl _objc_msgSend
    cbnz x0, failure
    adr x0, sel_run
    bl _sel_registerName
    mov x1, x0
    mov x0, x22
    bl _objc_msgSend
    mov x1, x23
    mov x0, x22
    bl _objc_msgSend
    cbz x0, failure
    adr x0, sel_terminate
    bl _sel_registerName
    mov x1, x0
    mov x0, x22
    mov x2, xzr
    bl _objc_msgSend
    mov x1, x23
    mov x0, x22
    bl _objc_msgSend
    cbnz x0, failure

    /* Return stored NSString UTF8String via Darwin BSD stdout. */
    adr x0, sel_utf8
    bl _sel_registerName
    mov x1, x0
    mov x0, x20
    bl _objc_msgSend
    mov x1, x0
    mov x0, #1
    mov x2, #66
    mov x16, #4
    svc #0x80
    mov x0, #0
    mov x16, #1
    svc #0x80

failure:
    mov x0, #97
    mov x16, #1
    svc #0x80

name_nsstring: .asciz "NSString"
name_nswindow: .asciz "NSWindow"
name_nsapp: .asciz "NSApplication"
sel_create: .asciz "stringWithUTF8String:"
sel_length: .asciz "length"
sel_new: .asciz "new"
sel_settitle: .asciz "setTitle:"
sel_present: .asciz "makeKeyAndOrderFront:"
sel_shared: .asciz "sharedApplication"
sel_running: .asciz "isRunning"
sel_run: .asciz "run"
sel_terminate: .asciz "terminate:"
sel_utf8: .asciz "UTF8String"
message: .asciz "BORDIFY COCOA: NSString NSWindow NSApplication lifecycle verified\n"
