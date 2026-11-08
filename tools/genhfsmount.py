#!/usr/bin/env python3
"""Create a deterministic zlib UDIF DMG with a minimal HFS+ catalog FILE.

The HFS+ volume includes an allocation bitmap, catalog B-tree header and a
real leaf record for DARWIN.MCH, whose data fork points at the separately
generated ARM64 Mach-O fixture. Its allocation metadata and fork extents
are constrained; it is NOT APFS, a PKG, an .app bundle or a commercial DMG.
"""
from pathlib import Path
import base64
import struct
import sys
import zlib

SECTOR = 512
BLOCK = 4096

def write_hfs_volume(app: bytes) -> bytearray:
    if not app.startswith(b"\xcf\xfa\xed\xfe") or len(app)>BLOCK:
        raise ValueError("Expected a <= 4096-byte ARM64 Mach-O")
    disk=bytearray(BLOCK*8)
    def b16(at, value): struct.pack_into(">H",disk,at,value)
    def b32(at, value): struct.pack_into(">I",disk,at,value)
    def b64(at, value): struct.pack_into(">Q",disk,at,value)
    vh=2*SECTOR
    b16(vh,0x482b);b16(vh+2,4)
    b32(vh+4,0)                # cleanly unmounted, no journal
    b32(vh+32,1)               # file count
    b32(vh+36,0)               # folders, excluding root
    b32(vh+40,BLOCK);b32(vh+44,8);b32(vh+48,1)
    # Volume allocation, extents overflow and attributes metadata forks.
    for offset,blockno in ((112,1),(192,5),(352,6)):
        b64(vh+offset,BLOCK)
        b32(vh+offset+12,1)
        b32(vh+offset+16,blockno)
        b32(vh+offset+20,1)
    b64(vh+272,2*BLOCK)  # catalog logical bytes
    b32(vh+284,2)
    b32(vh+288,2)
    b32(vh+292,2)
    # HFS+ bitmap uses MSB-first bits. Blocks 0..6 allocated.
    disk[BLOCK]=0xfe

    def btree_header(offset, nodes=8, nonempty=False):
        disk[offset+8]=1               # kBTHeaderNode
        b16(offset+10,3)
        b16(offset+14,1 if nonempty else 0) # depth
        b32(offset+16,1 if nonempty else 0) # root
        b32(offset+20,1 if nonempty else 0) # leaf records
        b32(offset+24,1 if nonempty else 0) # first leaf
        b32(offset+28,1 if nonempty else 0) # last leaf
        b16(offset+32,512)            # node size
        b32(offset+36,nodes)
        b32(offset+40,nodes-(2 if nonempty else 1))
        for at,value in ((510,14),(508,120),(506,248),(504,256)):
            b16(offset+at,value)

    catalog=2*BLOCK
    btree_header(catalog,16,True)
    btree_header(5*BLOCK)
    btree_header(6*BLOCK)

    leaf=catalog+SECTOR       # node #1
    disk[leaf+8]=0xff         # leaf node
    disk[leaf+9]=1            # height
    b16(leaf+10,1)            # one file record
    name="DARWIN.MCH"
    name_bytes=name.encode("utf-16-be")
    key_len=6+len(name_bytes)
    rec=leaf+14
    b16(rec,key_len)
    b32(rec+2,2)              # parent CNID = kHFSRootFolderID
    b16(rec+6,len(name))
    disk[rec+8:rec+8+len(name_bytes)]=name_bytes
    payload=rec+2+key_len
    b16(payload,2)            # kHFSPlusFileRecord
    b32(payload+8,16)         # file ID
    fork=payload+88
    b64(fork,len(app))
    b32(fork+12,1)            # fork total blocks
    b32(fork+16,4)            # first data block
    b32(fork+20,1)            # one extent block
    disk[4*BLOCK:4*BLOCK+len(app)]=app
    record_end=payload+248
    if record_end-leaf>506:
        raise ValueError("catalog record too long")
    b16(leaf+510,14)                         # start record 0
    b16(leaf+508,record_end-leaf)           # first free
    return disk


def main(output, application):
    raw=write_hfs_volume(Path(application).read_bytes())
    zdata=zlib.compress(raw,9)
    blk=bytearray(284)
    def be32(buf,at,val):struct.pack_into(">I",buf,at,val)
    def be64(buf,at,val):struct.pack_into(">Q",buf,at,val)
    blk[0:4]=b"mish";be32(blk,4,1)
    be64(blk,8,0);be64(blk,16,len(raw)//SECTOR)
    be64(blk,24,0);be32(blk,200,2)
    be32(blk,204,0x80000005)
    be64(blk,212,0);be64(blk,220,len(raw)//SECTOR)
    be64(blk,228,0);be64(blk,236,len(zdata))
    be32(blk,244,0xffffffff);be64(blk,252,len(raw)//SECTOR)
    xml=("<?xml version='1.0'?><plist><dict><key>resource-fork</key>"
         "<dict><key>blkx</key><array><dict><key>Data</key><data>\n"+
         base64.b64encode(blk).decode("ascii")+
         "\n</data></dict></array></dict></dict></plist>").encode("ascii")
    footer=bytearray(512)
    footer[:4]=b"koly"
    be32(footer,4,4);be32(footer,8,512)
    be64(footer,24,0);be64(footer,32,len(zdata))
    be64(footer,216,len(zdata));be64(footer,224,len(xml))
    be64(footer,492,len(raw)//SECTOR)
    dst=Path(output);dst.parent.mkdir(parents=True,exist_ok=True)
    dst.write_bytes(zdata+xml+footer)
    print(f"Generated {dst}: zlib UDIF / HFS+ root file DARWIN.MCH "
          f"({len(raw)} uncompressed bytes)")

if __name__=="__main__":
    if len(sys.argv)!=3:
        raise SystemExit("usage: genhfsmount.py output.dmg darwin.mch")
    main(sys.argv[1],sys.argv[2])
