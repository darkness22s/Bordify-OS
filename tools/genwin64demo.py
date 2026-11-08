#!/usr/bin/env python3
"""Generate a valid, standalone PE32+ AMD64 console *test* executable.

Uses the Microsoft x64 calling convention and real 64-bit PE import thunks.
The Bordify interpreter implements only the instructions/API calls used here.
This is neither a complete x64 emulator nor a working Windows installer.
"""
from pathlib import Path
import struct
import sys

BASE=0x140000000
MESSAGE=b"BORDIFY WIN64: PE32+ x64 WriteFile interpreted on x86 kernel\r\n"

def main(path):
    b=bytearray(0x800)
    def h(at,x): struct.pack_into("<H",b,at,x)
    def d(at,x): struct.pack_into("<I",b,at,x)
    def q(at,x): struct.pack_into("<Q",b,at,x)
    def raw(rva): return rva-0x1000+0x200
    b[0:2]=b"MZ";d(0x3c,0x80)
    pe=0x80;b[pe:pe+4]=b"PE\0\0"
    h(pe+4,0x8664);h(pe+6,1);h(pe+20,240);h(pe+22,0x22)
    o=pe+24;h(o,0x20b);d(o+16,0x1000);d(o+20,0x1000)
    q(o+24,BASE)
    d(o+32,0x1000);d(o+36,0x200)
    h(o+40,6);h(o+48,6);d(o+56,0x2000);d(o+60,0x200)
    h(o+68,3)
    q(o+72,0x4000);q(o+80,0x1000);q(o+88,0x4000);q(o+96,0x1000)
    d(o+108,16)
    d(o+120,0x1200);d(o+124,40)
    s=o+240;b[s:s+8]=b".text\0\0\0"
    d(s+8,0x600);d(s+12,0x1000);d(s+16,0x600);d(s+20,0x200)
    d(s+36,0xe0000020)
    d(raw(0x1200),0x1260)
    d(raw(0x1200)+12,0x1240)
    d(raw(0x1200)+16,0x1280)
    b[raw(0x1240):raw(0x1240)+13]=b"kernel32.dll\0"
    for i,rva in enumerate((0x12a0,0x12b0,0x12c0)):
        q(raw(0x1260)+i*8,rva)
        q(raw(0x1280)+i*8,rva)
    for rva,name in ((0x12a0,b"WriteFile\0"),(0x12b0,b"GetStdHandle\0"),
                     (0x12c0,b"ExitProcess\0")):
        b[raw(rva)+2:raw(rva)+2+len(name)]=name
    b[raw(0x12d0):raw(0x12d0)+len(MESSAGE)]=MESSAGE

    code=bytearray()
    code+=b"\x48\x83\xec\x28"   # Win64 shadow space, RSP 16-byte aligned
    def ripcall(iat):
        at=0x1000+len(code)
        code.extend(b"\xff\x15"+struct.pack("<i",iat-(at+6)))
    code+=b"\x48\xc7\xc1"+struct.pack("<i",-11) # RCX = STD_OUTPUT_HANDLE
    ripcall(0x1288)                              # GetStdHandle()
    code+=b"\x48\x89\xc1"                       # RCX = RAX
    at=0x1000+len(code)
    code+=b"\x48\x8d\x15"+struct.pack("<i",0x12d0-(at+7)) # RDX = msg
    code+=b"\x41\xb8"+struct.pack("<I",len(MESSAGE))        # R8D = bytes
    at=0x1000+len(code)
    code+=b"\x4c\x8d\x0d"+struct.pack("<i",0x1380-(at+7)) # R9 = DWORD bytesWritten
    # Home space reserved before the first imported function call.
    code+=b"\xc7\x44\x24\x20"+struct.pack("<I",0) # fifth arg NULL
    ripcall(0x1280)                              # WriteFile()
    code+=b"\x48\xc7\xc1"+struct.pack("<I",0) # exit status
    ripcall(0x1290)                              # ExitProcess()
    code+=b"\x0f\x0b"
    b[raw(0x1000):raw(0x1000)+len(code)]=code
    target=Path(path);target.parent.mkdir(parents=True,exist_ok=True)
    target.write_bytes(b)
    print(f"Generated {target}: PE32+ AMD64, 3 kernel32 imports, {len(code)} bytes code")

if __name__=="__main__":
    if len(sys.argv)!=2: raise SystemExit("usage: genwin64demo.py output.exe")
    main(sys.argv[1])
