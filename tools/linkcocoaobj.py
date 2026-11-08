#!/usr/bin/env python3
"""Bind three unresolved ARM64 Objective-C calls from a genuine Clang object.

Clang produces MH_OBJECT ARM64 with ARM64_RELOC_BRANCH26 relocations.
This restricted static binder reads LC_SEGMENT_64, LC_SYMTAB/nlist_64 and
those relocation entries, validates their symbol names, and fixes BL
targets to three SVC #0x82 stubs declared by a BORDIFY-OBJC LC_NOTE.

That LC_NOTE is a Bordify-specific compatibility extension; it is NOT
Apple dyld, chained fixups, the libobjc runtime or a Cocoa framework.
"""
from pathlib import Path
import struct
import sys

BASE=0x100000000
ENTRY=0x100
STUBS={"_objc_getClass":0x600,
       "_sel_registerName":0x604,
       "_objc_msgSend":0x608}
FILE_SIZE=0x800

def u32(blob, off):
    return struct.unpack_from("<I",blob,off)[0]

def get_name(strings, offset):
    if offset>=len(strings):
        raise ValueError("symbol name outside string table")
    last=strings.find(b"\0",offset)
    if last<0 or last-offset>128:
        raise ValueError("unterminated symbol")
    return strings[offset:last].decode("ascii")

def main(source, destination):
    obj=Path(source).read_bytes()
    if len(obj)<32 or u32(obj,0)!=0xfeedfacf or \
       u32(obj,4)!=0x0100000c or u32(obj,12)!=1:
        raise ValueError("expected Clang-assembled arm64 MH_OBJECT")
    count=u32(obj,16)
    size=u32(obj,20)
    if not 1<=count<=12 or size>len(obj)-32 or size<count*8:
        raise ValueError("bad load command list")
    off=32
    code=None
    reloc_off=reloc_count=0
    symbols=None
    for _ in range(count):
        if off+8>32+size:
            raise ValueError("truncated command")
        cmd,sz=struct.unpack_from("<II",obj,off)
        if sz<8 or off+sz>32+size:
            raise ValueError("invalid command size")
        if cmd==0x19: # LC_SEGMENT_64
            if sz<72:
                raise ValueError("invalid segment command")
            sections=u32(obj,off+64)
            if sections!=1 or sz!=72+80:
                raise ValueError("only one __TEXT,__text section is supported")
            section=off+72
            name=obj[section:section+16].split(b"\0",1)[0]
            segname=obj[section+16:section+32].split(b"\0",1)[0]
            code_len=struct.unpack_from("<Q",obj,section+40)[0]
            offset=u32(obj,section+48)
            reloc_off=u32(obj,section+56)
            reloc_count=u32(obj,section+60)
            if name!=b"__text" or segname!=b"__TEXT" or code_len<50 or \
               code_len>0x500 or offset+code_len>len(obj) or \
               reloc_count>64 or reloc_off+reloc_count*8>len(obj):
                raise ValueError("bad text/relocation range")
            code=bytearray(obj[offset:offset+code_len])
        elif cmd==0x2: # LC_SYMTAB
            if sz!=24 or symbols is not None:
                raise ValueError("bad symtab")
            symoff,nsyms,stroff,strsize=struct.unpack_from("<IIII",obj,off+8)
            if nsyms>128 or symoff+nsyms*16>len(obj) or \
               strsize>4096 or stroff+strsize>len(obj):
                raise ValueError("bad symbol range")
            strings=obj[stroff:stroff+strsize]
            symbols=[]
            for i in range(nsyms):
                at=symoff+i*16
                strx= u32(obj,at)
                ntype=obj[at+4]
                symbols.append((get_name(strings,strx),ntype))
        off+=sz
    if off!=32+size:
        raise ValueError("load commands must exactly fill declared command region")
    if code is None or symbols is None or reloc_count<3:
        raise ValueError("missing code/symbols/relocations")
    seen=set()
    seen_offsets=set()
    for i in range(reloc_count):
        where,info=struct.unpack_from("<iI",obj,reloc_off+i*8)
        symbol=info & 0x00ffffff
        pcrel=(info>>24)&1
        width=(info>>25)&3
        external=(info>>27)&1
        kind=(info>>28)&15
        if where<0 or where+4>len(code) or where%4 or \
           symbol>=len(symbols) or not pcrel or width!=2 or \
           not external or kind!=2: # ARM64_RELOC_BRANCH26
            raise ValueError("unsupported relocation")
        if where in seen_offsets:
            raise ValueError("overlapping ARM64 branch relocations")
        seen_offsets.add(where)
        name,ntype=symbols[symbol]
        if name not in STUBS or (ntype & 0x0e)!=0:
            raise ValueError("unrecognized or defined imported symbol")
        instruction=u32(code,where)
        if (instruction & 0xfc000000)!=0x94000000:
            raise ValueError("expected BL import call")
        stub=STUBS[name]
        diff=stub-(ENTRY+where)
        if diff%4 or not -(1<<27)<=diff<(1<<27):
            raise ValueError("BL branch out of range")
        struct.pack_into("<I",code,where,
                         0x94000000 | ((diff//4)&0x03ffffff))
        seen.add(name)
    if seen!=set(STUBS):
        raise ValueError("fixture must exercise all three Objective-C imports")

    output=bytearray(FILE_SIZE)
    struct.pack_into("<IIIIIIII",output,0,
                     0xfeedfacf,0x0100000c,0,2,3,136,0,0)
    struct.pack_into("<II16sQQQQIIII",output,32,0x19,72,
                     b"__TEXT".ljust(16,b"\0"),
                     BASE,4096,0,FILE_SIZE,5,5,0,0)
    struct.pack_into("<IIQQ",output,104,0x80000028,24,ENTRY,0)
    struct.pack_into("<II16sQQ",output,128,0x31,40,
                     b"BORDIFY-OBJC".ljust(16,b"\0"),0x700,32)
    output[ENTRY:ENTRY+len(code)]=code
    for i in range(3):
        struct.pack_into("<I",output,0x600+i*4,
                         0xd4000001 | (0x82<<5))
    output[0x700:0x708]=b"BOROBJ01"
    struct.pack_into("<IIIII",output,0x708,3,1,0x600,0x604,0x608)

    result=Path(destination);result.parent.mkdir(parents=True,exist_ok=True)
    result.write_bytes(output)
    print(f"Generated {result}: genuine Clang ARM64 code {len(code)} bytes, "
          f"{reloc_count} resolved BL imports, verified BORDIFY-OBJC LC_NOTE")

if __name__=="__main__":
    if len(sys.argv)!=3:
        raise SystemExit("usage: linkcocoaobj.py input.o output.mch")
    main(sys.argv[1],sys.argv[2])
