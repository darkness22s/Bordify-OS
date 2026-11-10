#!/usr/bin/env python3
"""Safe offline Win32/Win64 PE dependency scanner for real Windows programs.

This reads *only* file headers and import tables, never executes binaries.
It produces machine-readable evidence of what common Windows applications
require versus Bordify's very small existing Windows API subset.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path

BRIDGES = {
    "exitprocess", "getstdhandle", "writefile", "getlasterror",
    "createfilea", "readfile", "closehandle",
    "getcurrentprocessid", "getcurrentthreadid", "gettickcount",
    "getmodulehandlea", "getmodulehandlew",
    "getcommandlinea", "getcommandlinew",
    "setlasterror", "isdebuggerpresent",
    "virtualalloc", "virtualfree",
    "loadlibrarya", "getprocaddress", "freelibrary",
}
MSVCRT_BRIDGES = {
    "strlen", "memcpy", "memset", "memcmp", "strcmp", "strncmp",
    "malloc", "free", "calloc", "memmove", "realloc",
}
MAX_IMPORT_DESCRIPTORS = 2048
MAX_THUNKS_PER_DLL = 8192

def u16(buf, i):
    if i < 0 or i+2 > len(buf):
        raise ValueError(f"Truncated PE at {i} (u16)")
    return struct.unpack_from("<H", buf, i)[0]

def u32(buf, i):
    if i < 0 or i+4 > len(buf):
        raise ValueError(f"Truncated PE at {i} (u32)")
    return struct.unpack_from("<I", buf, i)[0]

def u64(buf, i):
    if i < 0 or i+8 > len(buf):
        raise ValueError(f"Truncated PE at {i} (u64)")
    return struct.unpack_from("<Q", buf, i)[0]

def cstr(buf, i, limit=160):
    if i < 0 or i >= len(buf):
        raise ValueError("Import string is outside PE")
    end = buf.find(b"\0", i, min(i+limit, len(buf)))
    if end < 0:
        raise ValueError("Import string is unterminated")
    return buf[i:end].decode("ascii", "replace")

def probe(path, max_bytes=1<<30):
    path = Path(path)
    size=path.stat().st_size
    if size<512 or size>max_bytes:
        raise ValueError(f"PE file invalid size: {size}")
    # Process a real-world installer as data in the CI runner, not in Bordify.
    with path.open("rb") as stream:
        content=stream.read()
    if content[:2]!=b"MZ":
        raise ValueError("Missing MZ signature")
    pe=u32(content,0x3c)
    if pe>size-24 or content[pe:pe+4]!=b"PE\0\0":
        raise ValueError("Missing PE signature")
    machine=u16(content,pe+4)
    section_count=u16(content,pe+6)
    opt_size=u16(content,pe+20)
    opt=pe+24
    if section_count>96 or opt_size<96 or opt+opt_size>size:
        raise ValueError("Invalid PE optional header or sections")
    magic=u16(content,opt)
    if magic not in (0x10b,0x20b):
        raise ValueError("Not PE32 or PE32+")
    is64=(magic==0x20b)
    entry=u32(content,opt+16)
    image_size=u32(content,opt+56)
    image_base=u64(content,opt+24) if is64 else u32(content,opt+28)
    header_size=u32(content,opt+60)
    subsystem=u16(content,opt+68)
    dirs=opt+(112 if is64 else 96)
    sections=[]
    end_on_disk=header_size
    for j in range(section_count):
        at=opt+opt_size+j*40
        if at+40>size: raise ValueError("Truncated section table")
        name=content[at:at+8].split(b"\0",1)[0].decode("ascii","replace")
        vsize=u32(content,at+8)
        rva=u32(content,at+12)
        rawsize=u32(content,at+16)
        raw=u32(content,at+20)
        if rawsize and (raw>size or rawsize>size-raw):
            raise ValueError("Section raw data exceeds file")
        end_on_disk=max(end_on_disk,raw+rawsize)
        sections.append({"name":name,"rva":rva,"virtual_bytes":vsize,
                         "raw_offset":raw,"raw_bytes":rawsize})
    def rva_to_file(rva, n=1):
        if rva < header_size and rva+n <= header_size and rva+n<=size:
            return rva
        for sec in sections:
            span=max(sec["virtual_bytes"],sec["raw_bytes"])
            if rva >= sec["rva"] and rva-sec["rva"]+n<=sec["raw_bytes"]:
                off=sec["raw_offset"]+rva-sec["rva"]
                if off+n<=size: return off
        raise ValueError(f"Unmapped PE RVA 0x{rva:x} (need {n})")
    dlls={}
    imports_truncated=False
    if opt+opt_size >= dirs+16 and u32(content,dirs+8):
        import_rva=u32(content,dirs+8)
        import_bytes=u32(content,dirs+12)
        for i in range(MAX_IMPORT_DESCRIPTORS):
            at=rva_to_file(import_rva+20*i,20)
            lookup=u32(content,at)
            dll_rva=u32(content,at+12)
            iat=u32(content,at+16)
            if not lookup and not dll_rva and not iat:
                break
            dll=cstr(content,rva_to_file(dll_rva)).lower()
            thunk=lookup if lookup else iat
            names=dlls.setdefault(dll,[])
            for j in range(MAX_THUNKS_PER_DLL):
                where=rva_to_file(thunk+j*(8 if is64 else 4),8 if is64 else 4)
                val=u64(content,where) if is64 else u32(content,where)
                if val==0: break
                if val&(1<<(63 if is64 else 31)):
                    names.append(f"#{val&0xffff}")
                else:
                    names.append(cstr(content,rva_to_file(val+2)))
            else: imports_truncated=True
        else: imports_truncated=True
    all_imports=sum(len(v) for v in dlls.values())
    # Distinguish missing modules (error 126) from missing exports (127).
    # This is a host-side estimate, not proof that the guest DLL loader can
    # load a given file or initialize its dependencies.
    supported_modules = {"kernel32.dll": BRIDGES, "msvcrt.dll": MSVCRT_BRIDGES}
    missing_modules = sorted(dll for dll in dlls if dll not in supported_modules)
    missing_exports = []
    unknown = []
    for dll, names in dlls.items():
        for name in names:
            if dll not in supported_modules:
                unknown.append(f"{dll}!{name}")
            elif name.lower() not in supported_modules[dll]:
                missing_exports.append(f"{dll}!{name}")
                unknown.append(f"{dll}!{name}")
    sha=hashlib.sha256(content).hexdigest()
    return {
        "filename":path.name,"bytes":size,"sha256":sha,
        "pe": "PE32+ AMD64" if is64 and machine==0x8664 else
              "PE32 x86" if not is64 and machine==0x14c else
              f"PE format magic=0x{magic:x}, machine=0x{machine:x}",
        "entry_rva":entry,"image_bytes":image_size,"image_base":f"0x{image_base:x}",
        "headers_bytes":header_size,"subsystem":subsystem,
        "section_count":section_count,"sections":sections,
        "overlay_bytes":max(0,size-end_on_disk),
        "dlls":dlls,"total_imports":all_imports,
        "missing_imports":unknown[:250],"missing_imports_count":len(unknown),
        "missing_modules":missing_modules,
        "missing_exports":missing_exports[:250],
        "missing_exports_count":len(missing_exports),
        "imports_truncated":imports_truncated,
        "fits_current_pe32_image":(
            not is64 and size<=2*1024*1024 and
            image_size<=1536*1024 and subsystem==3 and
            len(unknown)==0 and not imports_truncated),
        "has_pe_sections_within_64k":end_on_disk<=65536,
        "would_need_dll_loader":any(x!="kernel32.dll" for x in dlls),
    }

def main():
    p=argparse.ArgumentParser()
    p.add_argument("files",nargs="+",type=Path)
    p.add_argument("--output",required=True,type=Path)
    args=p.parse_args()
    all_results=[]
    for filename in args.files:
        try: result=probe(filename)
        except (ValueError,struct.error) as exc:
            result={"filename":filename.name,"error":str(exc)}
        all_results.append(result)
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(all_results,indent=2)+"\n")
    for result in all_results:
        if "error" in result:
            print(f"[winprobe] {result['filename']}: parse rejected: {result['error']}")
            continue
        print(f"[winprobe] {result['filename']}: {result['pe']}, "
              f"file={result['bytes']}, image={result['image_bytes']}, "
              f"overlay={result['overlay_bytes']}, base={result['image_base']}, "
              f"imports={result['total_imports']} ({len(result['dlls'])} DLLs), "
              f"unsupported={result['missing_imports_count']}, "
              f"PE32-supported={result['fits_current_pe32_image']}")
        if result["missing_modules"]:
            print("[winprobe] missing DLL modules (guest error 126 candidate): "
                  + ", ".join(result["missing_modules"]))
        if result["missing_exports"]:
            print("[winprobe] missing exports (guest error 127 candidate): "
                  + ", ".join(result["missing_exports"][:12]))
        for dll,names in sorted(result["dlls"].items()):
            print(f"[winprobe]   {dll}: {len(names)} imports, sample={', '.join(names[:12])}")
    print("[winprobe] These are host-side PE metadata checks, not execution.")

if __name__=="__main__":
    main()
