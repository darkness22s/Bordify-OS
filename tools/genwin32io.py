#!/usr/bin/env python3
"""Build a real PE32 console EXE using x86 stdcall imports from kernel32.

The test program creates C:\BORDIFY\WIN32.TXT, writes a byte sequence,
closes the file, reopens it, reads it back, compares every byte in ring 3
and prints to STDOUT using Win32 WriteFile before ExitProcess(0).
This is a narrow controlled file-I/O regression fixture, not general Wine.
"""
from pathlib import Path
import struct
import sys

BASE=0x40000000
START=0x1000
CODE_LIMIT=0x1200
MSG=b"BORDIFY WIN32 FILE: CreateFileA WriteFile ReadFile CloseHandle verified\r\n"
PATH=b"C:\\BORDIFY\\WIN32.TXT\0"

def main(target):
    b=bytearray(0xA00)
    def u16(at,v):struct.pack_into("<H",b,at,v)
    def u32(at,v):struct.pack_into("<I",b,at,v)
    def raw(rva):
        assert 0x1000<=rva<0x1800
        return rva-0x1000+0x200
    b[:2]=b"MZ"
    u32(0x3c,0x80)
    pe=0x80;b[pe:pe+4]=b"PE\0\0"
    u16(pe+4,0x014c);u16(pe+6,1);u16(pe+20,224);u16(pe+22,0x0102)
    o=pe+24
    u16(o,0x10b);u32(o+16,START);u32(o+20,0x1000)
    u32(o+24,0x1000);u32(o+28,BASE)
    u32(o+32,0x1000);u32(o+36,0x200)
    u16(o+40,4);u16(o+48,4)
    u32(o+56,0x2000);u32(o+60,0x200);u16(o+68,3)
    u32(o+72,0x4000);u32(o+76,0x1000);u32(o+80,0x4000)
    u32(o+84,0x1000);u32(o+92,16)
    u32(o+104,0x1200);u32(o+108,40)
    sec=o+224
    b[sec:sec+8]=b".text\0\0\0"
    u32(sec+8,0x800);u32(sec+12,0x1000)
    u32(sec+16,0x800);u32(sec+20,0x200)
    u32(sec+36,0xe0000020)
    u32(raw(0x1200),0x1260)
    u32(raw(0x1200)+12,0x1240)
    u32(raw(0x1200)+16,0x1290)
    b[raw(0x1240):raw(0x1240)+13]=b"kernel32.dll\0"
    imports=[
        (0x12b0,b"CreateFileA\0"),
        (0x12c0,b"WriteFile\0"),
        (0x12d0,b"ReadFile\0"),
        (0x12e0,b"CloseHandle\0"),
        (0x12f0,b"GetStdHandle\0"),
        (0x1308,b"GetLastError\0"),
        (0x1320,b"ExitProcess\0")
    ]
    for i,(rva,name) in enumerate(imports):
        u32(raw(0x1260)+4*i,rva)
        u32(raw(0x1290)+4*i,rva)
        b[raw(rva)+2:raw(rva)+2+len(name)]=name
    b[raw(0x1350):raw(0x1350)+len(PATH)]=PATH
    b[raw(0x1380):raw(0x1380)+len(MSG)]=MSG

    code=bytearray()
    patches=[]
    def emit(v):code.extend(v)
    def push(x):
        emit(b"\x68"+struct.pack("<I",x&0xffffffff))
    def ptr(rva):return BASE+rva
    def call_index(i):emit(b"\xff\x15"+struct.pack("<I",ptr(0x1290+i*4)))
    def test_true():
        emit(b"\x83\xf8\x01")             # cmp eax,1
        patches.append(len(code)+2)
        emit(b"\x0f\x85"+b"\0"*4)         # jne fail
    def cmp_count(rva,n):
        emit(b"\x81\x3d"+struct.pack("<I",ptr(rva))+
             struct.pack("<I",n))
        patches.append(len(code)+2)
        emit(b"\x0f\x85"+b"\0"*4)
    def create(access,mode):
        for x in (0,0x80,mode,0,0,access,ptr(0x1350)):
            push(x)
        call_index(0)
        emit(b"\x89\xc3")                 # mov ebx,eax
    def io(index, buffer, size, count_rva):
        push(0)
        push(ptr(count_rva))
        push(size)
        push(ptr(buffer))
        emit(b"\x53")                     # push ebx
        call_index(index)
        test_true()
        cmp_count(count_rva,size)

    create(0x40000000,2)                 # GENERIC_WRITE,CREATE_ALWAYS
    io(1,0x1380,len(MSG),0x1500)         # WriteFile
    emit(b"\x53")                       # push ebx
    call_index(3)                       # CloseHandle
    test_true()
    create(0x80000000,3)                 # GENERIC_READ, OPEN_EXISTING
    io(2,0x1400,len(MSG),0x1504)         # ReadFile
    emit(b"\xbe"+struct.pack("<I",ptr(0x1380))) # mov esi,msg
    emit(b"\xbf"+struct.pack("<I",ptr(0x1400))) # mov edi,readback
    emit(b"\xb9"+struct.pack("<I",len(MSG)))    # mov ecx,len
    emit(b"\xf3\xa6")                   # repe cmpsb
    patches.append(len(code)+2)
    emit(b"\x0f\x85"+b"\0"*4)         # jne fail
    emit(b"\x53")
    call_index(3)
    test_true()
    push(-11)
    call_index(4)                        # GetStdHandle
    emit(b"\x89\xc3")                  # mov ebx,eax
    push(0);push(0);push(len(MSG));push(ptr(0x1380))
    emit(b"\x53")
    call_index(1)                        # stdout WriteFile
    test_true()
    push(0)
    call_index(6)
    emit(b"\x0f\x0b")                   # forbidden fallthrough
    error_at=len(code)
    call_index(5)                        # GetLastError: use if nonzero
    emit(b"\x85\xc0")                  # test eax,eax
    emit(b"\x75\x05")                  # jne +5
    emit(b"\xb8\x01\0\0\0")           # mov eax,1
    emit(b"\x50")                       # push eax
    call_index(6)                        # ExitProcess(error or 1)
    emit(b"\x0f\x0b")
    for p in patches:
        struct.pack_into("<i",code,p,error_at-(p+4))
    if len(code)>CODE_LIMIT-START:
        raise ValueError(f"Win32 test code {len(code)} bytes exceeds 512-byte window")
    b[raw(START):raw(START)+len(code)]=code
    out=Path(target);out.parent.mkdir(parents=True,exist_ok=True)
    out.write_bytes(b)
    print(f"Generated {out}: PE32 stdcall file roundtrip, 7 kernel32 imports, "
          f"{len(code)} code bytes, {len(MSG)} persistent data bytes")

if __name__=="__main__":
    if len(sys.argv)!=2:raise SystemExit("usage: genwin32io.py output.exe")
    main(sys.argv[1])
