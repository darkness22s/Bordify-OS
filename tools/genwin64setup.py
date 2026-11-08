#!/usr/bin/env python3
"""Build a deterministic, legitimate PE32+ x86-64 installer sample.

The setup EXE imports actual Win64 kernel32 CreateFileA, WriteFile,
CloseHandle and ExitProcess with Microsoft x64 calling conventions, then
writes a second PE32+ executable to C:\APP64.EXE.

Bordify maps ONLY this audited path to /APPS/APP64.EXE, on a single
isolated FAT16 disk. This is NOT NSIS/Inno/MSI or general installer support.
"""
from pathlib import Path
import struct
import sys

def main(output, payload):
    app=Path(payload).read_bytes()
    if len(app)!=0x800 or app[:2]!=b"MZ":
        raise ValueError("expected generated standalone PE32+ app fixture")
    b=bytearray(0xE00)
    b[:0x800]=app
    def d(at,x):struct.pack_into("<I",b,at,x)
    def q(at,x):struct.pack_into("<Q",b,at,x)
    def raw(rva):return rva-0x1000+0x200
    # Single executable .text section extends through embedded payload.
    section=0x188
    d(section+8,0xC00)  # virtual section size
    d(section+16,0xC00) # raw section size
    # Keep the 0x2000 SizeOfImage.
    for i in range(0x200,len(b)):
        b[i]=0
    d(raw(0x1200),0x1260)   # import lookup table
    d(raw(0x1200)+12,0x1240) # DLL name
    d(raw(0x1200)+16,0x1290) # IAT; lookup table needs fifth NULL slot
    b[raw(0x1240):raw(0x1240)+13]=b"kernel32.dll\0"
    imports=((0x12C0,b"CreateFileA\0"),
             (0x12D8,b"WriteFile\0"),
             (0x12F0,b"CloseHandle\0"),
             (0x1308,b"ExitProcess\0"))
    for i,(rva,name) in enumerate(imports):
        q(raw(0x1260)+i*8,rva)
        q(raw(0x1290)+i*8,rva)
        b[raw(rva)+2:raw(rva)+2+len(name)]=name
    path=b"C:\\APP64.EXE\0"
    b[raw(0x1350):raw(0x1350)+len(path)]=path
    b[raw(0x1400):raw(0x1400)+len(app)]=app

    code=bytearray()
    def add(ins):code.extend(ins)
    def lea(reg, rva):
        at=0x1000+len(code)
        add(b"\x48\x8d"+bytes([reg])+struct.pack("<i",rva-(at+7)))
    def call(iat):
        at=0x1000+len(code)
        add(b"\xff\x15"+struct.pack("<i",iat-(at+6)))
    def mov_stack(offset,value):
        add(b"\xc7\x44\x24"+bytes([offset])+struct.pack("<I",value))
    def mov_rcx(value):
        add(b"\x48\xc7\xc1"+struct.pack("<i",value))
    def mov_r8(value):add(b"\x41\xb8"+struct.pack("<I",value))

    add(b"\x48\x83\xec\x38")  # 7-argument CreateFileA ABI shadow space
    lea(0x0d,0x1350)            # RCX -> "C:\APP64.EXE"
    add(b"\xba"+struct.pack("<I",0x40000000)) # EDX=GENERIC_WRITE
    mov_r8(0)                   # share mode=0
    add(b"\x45\x31\xc9")       # security attributes=null
    mov_stack(0x20,2)           # CREATE_ALWAYS
    mov_stack(0x28,0x80)        # FILE_ATTRIBUTE_NORMAL
    mov_stack(0x30,0)           # template=null
    call(0x1290)                # CreateFileA
    add(b"\x48\x89\xc3")       # rbx=rax (save file handle)
    add(b"\x48\x89\xc1")       # rcx=rax (file handle)
    lea(0x15,0x1400)            # rdx -> embedded PE32+ program
    mov_r8(len(app))
    at=0x1000+len(code)
    add(b"\x4c\x8d\x0d"+struct.pack("<i",0x13c0-(at+7)))
    # R9 points to a writable DWORD lpNumberOfBytesWritten
    mov_stack(0x20,0)           # lpOverlapped=NULL
    call(0x1298)                # WriteFile
    add(b"\x48\x89\xd9")       # rcx=rbx
    call(0x12A0)                # CloseHandle -> commit
    mov_rcx(0)
    call(0x12A8)                # ExitProcess(0)
    add(b"\x0f\x0b")            # reject accidental fallthrough
    if len(code)>=0x1E0:
        raise ValueError("test VM code too long for fixed fixture layout")
    b[raw(0x1000):raw(0x1000)+len(code)]=code
    target=Path(output);target.parent.mkdir(parents=True,exist_ok=True)
    target.write_bytes(b)
    print(f"Generated {target}: x64 PE32+ installer fixture, {len(imports)} imports, payload {len(app)} bytes")

if __name__=="__main__":
    if len(sys.argv)!=3:
        raise SystemExit("usage: genwin64setup.py output.exe compiled-app.exe")
    main(sys.argv[1],sys.argv[2])
