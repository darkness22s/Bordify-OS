#!/usr/bin/env python3
"""Build a genuine ARM64 Mach-O calling an experimental Darwin/ObjC guest ABI.

SVC #0x80 models BSD syscall numbers 4/write and 1/exit.
SVC #0x81 routes modeled objc_getClass/objc_msgSend, NOT real dyld/libobjc.
Exercises NSString stringWithUTF8String:/UTF8String, and
NSApplication sharedApplication/isRunning. No Cocoa window or app bundle.
"""
from pathlib import Path
import struct
import sys
BASE=0x100000000
START=0x100
TEXT=(b"BORDIFY DARWIN: NSString objc_msgSend and BSD write on ARM64\n")

def movz(reg,n):
    assert 0<=n<65536
    return 0xd2800000 | (n<<5) | reg
def adr(reg, dest, here):
    off=dest-here
    assert -(1<<20)<=off<(1<<20)
    off &= (1<<21)-1
    return 0x10000000 | ((off&3)<<29) | ((off>>2)<<5) | reg
def svc(num): return 0xd4000001 | (num<<5)

def main(path):
    buf=bytearray(1024)
    struct.pack_into("<IIIIIIII",buf,0,
                     0xfeedfacf,0x0100000c,0,2,2,96,0,0)
    struct.pack_into("<II16sQQQQIIII",buf,32,0x19,72,
                     b"__TEXT".ljust(16,b"\0"),BASE,4096,0,len(buf),5,5,0,0)
    struct.pack_into("<IIQQ",buf,104,0x80000028,24,START,0)
    strings={0x200:b"NSString\0",
             0x220:b"stringWithUTF8String:\0",
             0x240:b"UTF8String\0",
             0x260:b"NSApplication\0",
             0x280:b"sharedApplication\0",
             0x2a0:b"isRunning\0",
             0x2c0:TEXT+b"\0"}
    for at,s in strings.items():buf[at:at+len(s)]=s

    code=[]
    def a(reg,at): code.append(adr(reg,BASE+at,BASE+START+4*len(code)))
    def m(reg,x):code.append(movz(reg,x))
    def trap(n):code.append(svc(n))
    a(0,0x200);m(8,11);trap(0x81)       # objc_getClass("NSString")
    a(1,0x220);a(2,0x2c0);m(8,13);trap(0x81) # msgSend: stringWithUTF8String:
    a(1,0x240);m(8,13);trap(0x81)     # msgSend: UTF8String => x0
    code.append(0xaa0003e1)           # mov x1,x0 pointer returned by NSString
    m(0,1);m(2,len(TEXT));m(16,4);trap(0x80) # Darwin BSD write(1,ptr,len)
    a(0,0x260);m(8,11);trap(0x81)     # getClass("NSApplication")
    a(1,0x280);m(8,13);trap(0x81)     # sharedApplication => object
    a(1,0x2a0);m(8,13);trap(0x81)     # isRunning => false
    m(16,1);trap(0x80)                # Darwin exit(0)
    struct.pack_into("<"+"I"*len(code),buf,START,*code)
    output=Path(path);output.parent.mkdir(parents=True,exist_ok=True)
    output.write_bytes(buf)
    print(f"Generated {output}: Mach-O ARM64 Darwin BSD + ObjC bridge ({len(code)} instructions)")

if __name__=="__main__":
    if len(sys.argv)!=2:raise SystemExit("usage: gendarwindemo.py output.mch")
    main(sys.argv[1])
