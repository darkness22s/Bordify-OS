#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <zlib.h>
#include "package_inspect.h"
#include "udif_blkx.h"
#include "hfsplus_read.h"

static uint8_t dmg[128*1024];
static uint8_t volume[64*1024];
static uint8_t payload[HFSPLUS_MAX_EXTRACT];
static size_t dmg_size;

typedef struct {
    const uint8_t *bytes;
    size_t size;
    bool fail;
} source_t;
static int read_bytes(void *ctx,uint64_t at,void *out,size_t n) {
    source_t *src=(source_t*)ctx;
    if(src->fail || at>src->size || n>src->size-(size_t)at)
        return -1;
    memcpy(out,src->bytes+(size_t)at,n);
    return (int)n;
}
static udif_block_map_t map;
static void raw_map(void) {
    memset(&map,0,sizeof(map));
    map.sector_start=0;map.sector_count=64;
    map.runs_count=1;
    map.runs[0].type=1;
    map.runs[0].sectors=64;
    map.runs[0].length=32768;
}
static void be32set(uint8_t *p,unsigned x) {
    p[0]=(uint8_t)(x>>24);p[1]=(uint8_t)(x>>16);
    p[2]=(uint8_t)(x>>8);p[3]=(uint8_t)x;
}
int main(void) {
    FILE *f=fopen("build/apps/mount.dmg","rb");
    assert(f);
    dmg_size=fread(dmg,1,sizeof(dmg),f);
    fclose(f);
    assert(dmg_size>512 && dmg_size<sizeof(dmg));

    package_info_t header;
    assert(package_inspect_dmg_trailer(dmg+dmg_size-512,512,dmg_size,
                                       &header) && header.valid);
    assert(header.metadata_length<4096 &&
           header.metadata_offset+header.metadata_length<=dmg_size-512);
    assert(udif_blkx_from_xml((const char*)dmg+header.metadata_offset,
                               (size_t)header.metadata_length,
                               header.data_offset,dmg_size,&map));
    source_t source={dmg,dmg_size,false};
    hfsplus_mount_t mount;
    assert(hfsplus_mount_readonly(&mount,&map,read_bytes,&source));
    hfsplus_file_t file;
    assert(hfsplus_find_root_file(&mount,"DARWIN.MCH",&file));
    assert(file.file_id==16 && file.first_block==4 &&
           file.extent_blocks==1 && file.size==1024);
    assert(!hfsplus_find_root_file(&mount,"NOTHERE.MCH",&file));
    assert(hfsplus_find_root_file(&mount,"DARWIN.MCH",&file));
    assert(!hfsplus_read_file(&mount,&file,payload,file.size-1));
    assert(hfsplus_read_file(&mount,&file,payload,sizeof(payload)));

    uint8_t original[4096];
    f=fopen("build/apps/darwin.mch","rb");assert(f);
    size_t original_size=fread(original,1,sizeof(original),f);
    fclose(f);
    assert(original_size==file.size &&
           !memcmp(payload,original,original_size));
    source.fail=true;
    assert(!hfsplus_read_file(&mount,&file,payload,sizeof(payload)));
    source.fail=false;

    /* Independently decompress to inspect and fuzz real HFS+ structures,
       then feed through the same UDIF raw-sector callback. */
    uLongf raw_len=sizeof(volume);
    assert(uncompress(volume,&raw_len,dmg,map.runs[0].length)==Z_OK);
    assert(raw_len==32768);
    raw_map();
    source.bytes=volume;source.size=(size_t)raw_len;
    assert(hfsplus_mount_readonly(&mount,&map,read_bytes,&source));
    assert(hfsplus_find_root_file(&mount,"DARWIN.MCH",&file));
    assert(hfsplus_read_file(&mount,&file,payload,sizeof(payload)) &&
           !memcmp(payload,original,original_size));

    size_t leaf=2*4096+512;
    volume[leaf+8]=0; /* invalid B-tree leaf */
    assert(!hfsplus_find_root_file(&mount,"DARWIN.MCH",&file));
    volume[leaf+8]=0xff;
    volume[leaf+510]=0; /* invalid record offset */
    assert(!hfsplus_find_root_file(&mount,"DARWIN.MCH",&file));
    volume[leaf+510]=14;
    volume[leaf+14+2+3]=7; /* parentID no longer root */
    assert(!hfsplus_find_root_file(&mount,"DARWIN.MCH",&file));
    volume[leaf+14+2+3]=2;
    assert(hfsplus_find_root_file(&mount,"DARWIN.MCH",&file));
    size_t key_len=6+2*strlen("DARWIN.MCH");
    size_t fork=leaf+14+2+key_len+88;
    be32set(volume+fork+16,99); /* first extent outside image */
    assert(!hfsplus_find_root_file(&mount,"DARWIN.MCH",&file));
    be32set(volume+fork+16,4);
    volume[2*512]=0; /* destroy HFS+ signature */
    assert(!hfsplus_mount_readonly(&mount,&map,read_bytes,&source));

    puts("UDIF zlib HFS+ read-only mount, catalog lookup and extraction: PASS");
    return 0;
}
